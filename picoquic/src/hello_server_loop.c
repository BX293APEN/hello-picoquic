/*
 * hello_server_loop.c
 *
 * echo_loop.c (penquic クライアント) と繰り返し通信する QUIC エコーサーバー。
 *
 * 動作仕様:
 *   - ALPN "echo"、デフォルトポート 7777 で待ち受け
 *   - クライアントが 1 メッセージごとに新しい双方向ストリームを開く
 *   - ストリームに届いたデータを蓄積し、クライアントから FIN が来たら
 *     受信データをそのまま全部エコーして FIN 付きで返す
 *   - コネクションは切断しない (次のストリームを待ち続ける)
 *   - 複数のストリームを同時に扱えるよう、ストリームごとにコンテキストを管理
 *
 * OpenSSL 不使用 (picotls minicrypto バックエンド)。
 * H3 / QPACK 不使用。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>   /* AF_INET */

#include <picoquic.h>
#include <picoquic_utils.h>
#include <picosocks.h>
#include <picoquic_packet_loop.h>

/* ------------------------------------------------------------------ */
/* 設定                                                                 */
/* ------------------------------------------------------------------ */

#define ECHO_ALPN        "echo"
#define DEFAULT_PORT     7777
#define RECV_BUF_INIT    4096   /* 受信バッファ初期サイズ */

/* ------------------------------------------------------------------ */
/* ストリームごとのコンテキスト (受信データを蓄積して echo する)        */
/* ------------------------------------------------------------------ */

typedef struct st_echo_stream_ctx_t {
    uint64_t  stream_id;

    /* 受信データ蓄積バッファ */
    uint8_t  *recv_buf;
    size_t    recv_cap;     /* アロケーション済みサイズ */
    size_t    recv_len;     /* 蓄積済みバイト数 */

    /* 送信制御 */
    int       fin_received; /* クライアントから FIN を受け取った */
    int       echo_sent;    /* echo 送信完了 */

    /* 連結リスト (同一接続内の複数ストリームを追跡) */
    struct st_echo_stream_ctx_t *next;
    struct st_echo_stream_ctx_t *prev;
} echo_stream_ctx_t;

/* ------------------------------------------------------------------ */
/* 接続ごとのコンテキスト                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    echo_stream_ctx_t *stream_head; /* ストリームコンテキストのリスト先頭 */
} echo_server_ctx_t;

/* ------------------------------------------------------------------ */
/* ストリームコンテキストのユーティリティ                               */
/* ------------------------------------------------------------------ */

static echo_stream_ctx_t *stream_ctx_new(uint64_t stream_id)
{
    echo_stream_ctx_t *sc = (echo_stream_ctx_t *)calloc(1, sizeof(*sc));
    if (!sc) return NULL;

    sc->recv_buf = (uint8_t *)malloc(RECV_BUF_INIT);
    if (!sc->recv_buf) {
        free(sc);
        return NULL;
    }
    sc->recv_cap = RECV_BUF_INIT;
    sc->stream_id = stream_id;
    return sc;
}

static void stream_ctx_free(echo_stream_ctx_t *sc)
{
    if (sc) {
        free(sc->recv_buf);
        free(sc);
    }
}

/* リストから切り離して解放 */
static void stream_ctx_remove(echo_server_ctx_t *srv, echo_stream_ctx_t *sc)
{
    if (sc->prev) sc->prev->next = sc->next;
    else          srv->stream_head = sc->next;
    if (sc->next) sc->next->prev = sc->prev;
    stream_ctx_free(sc);
}

/* 受信バッファにデータを追記 (必要なら拡張) */
static int stream_ctx_append(echo_stream_ctx_t *sc, const uint8_t *data, size_t len)
{
    if (len == 0) return 0;

    size_t new_len = sc->recv_len + len;
    if (new_len > sc->recv_cap) {
        size_t new_cap = sc->recv_cap * 2;
        while (new_cap < new_len) new_cap *= 2;
        uint8_t *p = (uint8_t *)realloc(sc->recv_buf, new_cap);
        if (!p) return -1;
        sc->recv_buf = p;
        sc->recv_cap = new_cap;
    }
    memcpy(sc->recv_buf + sc->recv_len, data, len);
    sc->recv_len += len;
    return 0;
}

/* ------------------------------------------------------------------ */
/* サーバーコールバック                                                  */
/* ------------------------------------------------------------------ */

static int echo_server_callback(
    picoquic_cnx_t            *cnx,
    uint64_t                   stream_id,
    uint8_t                   *bytes,
    size_t                     length,
    picoquic_call_back_event_t fin_or_event,
    void                      *callback_ctx,
    void                      *v_stream_ctx)
{
    echo_server_ctx_t *srv_ctx    = (echo_server_ctx_t *)callback_ctx;
    echo_stream_ctx_t *stream_ctx = (echo_stream_ctx_t *)v_stream_ctx;

    /* ---- 新規接続: コンテキストを割り当て ---- */
    if (srv_ctx == NULL ||
        srv_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx)))
    {
        echo_server_ctx_t *nc = (echo_server_ctx_t *)calloc(1, sizeof(*nc));
        if (!nc) {
            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return -1;
        }
        picoquic_set_callback(cnx, echo_server_callback, nc);
        srv_ctx = nc;
    }

    switch (fin_or_event) {

    /* ----------------------------------------------------------------
     * データ受信 / FIN 受信
     * ---------------------------------------------------------------- */
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        /*
         * クライアント発の双方向ストリームのみ処理する。
         * echo_loop.c は UINT64_MAX (=新規ストリーム) を毎回指定して
         * 1 メッセージにつき 1 ストリームを開く。
         */
        if (!PICOQUIC_IS_CLIENT_STREAM_ID(stream_id) ||
            !PICOQUIC_IS_BIDIR_STREAM_ID(stream_id))
        {
            break;
        }

        /* 初めて届いたストリームならコンテキストを作成 */
        if (stream_ctx == NULL) {
            stream_ctx = stream_ctx_new(stream_id);
            if (!stream_ctx) {
                fprintf(stderr, "[server] OOM: stream_ctx_new\n");
                return -1;
            }
            if (picoquic_set_app_stream_ctx(cnx, stream_id, stream_ctx) != 0) {
                stream_ctx_free(stream_ctx);
                return -1;
            }
            /* リストの先頭に挿入 */
            stream_ctx->next = srv_ctx->stream_head;
            if (srv_ctx->stream_head) srv_ctx->stream_head->prev = stream_ctx;
            srv_ctx->stream_head = stream_ctx;
        }

        /* データを蓄積 */
        if (length > 0) {
            if (stream_ctx_append(stream_ctx, bytes, length) != 0) {
                fprintf(stderr, "[server] OOM: stream_ctx_append\n");
                return -1;
            }
            printf("[server] stream_id=%" PRIu64 "  +%zu bytes  (total=%zu)\n",
                   stream_id, length, stream_ctx->recv_len);
        }

        /* FIN を受信したら echo 送信をスケジュール */
        if (fin_or_event == picoquic_callback_stream_fin && !stream_ctx->fin_received) {
            stream_ctx->fin_received = 1;
            printf("[server] stream_id=%" PRIu64 "  FIN received. Scheduling echo (%zu bytes).\n",
                   stream_id, stream_ctx->recv_len);

            /* picoquic に「このストリームで送信データがある」と知らせる */
            int ret = picoquic_mark_active_stream(cnx, stream_id, 1, stream_ctx);
            if (ret != 0) {
                fprintf(stderr, "[server] mark_active_stream failed: %d\n", ret);
            }
        }
        break;

    /* ----------------------------------------------------------------
     * ゼロコピー送信コールバック: 蓄積したデータを全部 echo する
     * ---------------------------------------------------------------- */
    case picoquic_callback_prepare_to_send:
        if (stream_ctx == NULL || stream_ctx->echo_sent || !stream_ctx->fin_received) {
            break;
        }

        {
            size_t  to_send = stream_ctx->recv_len;
            size_t  avail   = (to_send <= length) ? to_send : length;

            /*
             * picoquic_provide_stream_data_buffer:
             *   引数: (context, nb_bytes, is_fin, is_still_active)
             *   戻り値: 書き込み先ポインタ (NULL = 失敗)
             *
             * FIN を同時に立てて、1 回の呼び出しで全データ+FINを送る。
             * echo データが 1 回の呼び出しで収まらない場合は is_still_active=1
             * にして次回も呼んでもらうが、RECV_BUF_INIT を十分大きく確保している
             * ので通常は 1 回で完了する。
             */
            int is_fin         = (avail == to_send) ? 1 : 0;
            int is_still_active = (avail < to_send) ? 1 : 0;

            uint8_t *buf = picoquic_provide_stream_data_buffer(
                bytes, avail, is_fin, is_still_active);

            if (buf) {
                memcpy(buf, stream_ctx->recv_buf, avail);

                /* 送信済み分をバッファの先頭に詰め直す */
                stream_ctx->recv_len -= avail;
                if (stream_ctx->recv_len > 0) {
                    memmove(stream_ctx->recv_buf,
                            stream_ctx->recv_buf + avail,
                            stream_ctx->recv_len);
                }

                if (is_fin) {
                    stream_ctx->echo_sent = 1;
                    printf("[server] stream_id=%" PRIu64
                           "  echo sent (%zu bytes) + FIN.\n",
                           stream_ctx->stream_id, avail);
                }
            }
        }
        break;

    /* ----------------------------------------------------------------
     * ピアがストリームをリセット / 送信停止を要求
     * ---------------------------------------------------------------- */
    case picoquic_callback_stream_reset:
    case picoquic_callback_stop_sending:
        if (stream_ctx) {
            picoquic_reset_stream(cnx, stream_id, 0);
            stream_ctx_remove(srv_ctx, stream_ctx);
            picoquic_set_app_stream_ctx(cnx, stream_id, NULL);
        }
        break;

    /* ----------------------------------------------------------------
     * 接続クローズ: 全ストリームコンテキストを解放
     * ---------------------------------------------------------------- */
    case picoquic_callback_stateless_reset:
    case picoquic_callback_close:
    case picoquic_callback_application_close:
        printf("[server] connection closed.\n");
        if (srv_ctx) {
            echo_stream_ctx_t *sc = srv_ctx->stream_head;
            while (sc) {
                echo_stream_ctx_t *next = sc->next;
                stream_ctx_free(sc);
                sc = next;
            }
            free(srv_ctx);
        }
        picoquic_set_callback(cnx, NULL, NULL);
        break;

    case picoquic_callback_almost_ready:
        printf("[server] connection almost ready.\n");
        break;

    case picoquic_callback_ready:
        printf("[server] connection ready. Waiting for echo requests...\n");
        break;

    default:
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* エントリーポイント                                                    */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int         server_port = DEFAULT_PORT;
    const char *cert_file   = "certs/cert.pem";
    const char *key_file    = "certs/key.pem";

    if (argc >= 2) server_port = atoi(argv[1]);
    if (argc >= 3) cert_file   = argv[2];
    if (argc >= 4) key_file    = argv[3];

    printf("Starting echo-QUIC server (ALPN=\"%s\") on port %d\n",
           ECHO_ALPN, server_port);
    printf("  cert: %s\n", cert_file);
    printf("  key : %s\n", key_file);

    uint64_t current_time = picoquic_current_time();

    picoquic_quic_t *quic = picoquic_create(
        64,         /* max_nb_connections: 複数クライアントに対応 */
        cert_file,
        key_file,
        NULL,       /* CA root (サーバーは不要) */
        ECHO_ALPN,
        echo_server_callback,
        NULL,       /* default callback ctx: 接続ごとに割り当て */
        NULL, NULL, NULL,
        current_time,
        NULL,
        NULL, NULL, 0);

    if (!quic) {
        fprintf(stderr, "[server] Failed to create QUIC context."
                        " Check cert/key paths.\n");
        return 1;
    }

    /* デフォルトコンテキストを識別用マーカーとしてセット */
    echo_server_ctx_t default_marker = {0};
    picoquic_set_default_callback(quic, echo_server_callback, &default_marker);

    /*
     * picoquic_packet_loop:
     *   - server_port で bind して受信ループを開始する
     *   - AF_INET を明示して IPv6 非対応環境でのエラーを回避
     *   - ループコールバック (最後の 2 引数) は NULL でも動く
     */
    int ret = picoquic_packet_loop(
        quic, server_port, AF_INET,
        0, 0, 0,
        NULL, NULL);

    printf("[server] exiting, ret=%d\n", ret);
    picoquic_free(quic);
    return (ret == 0) ? 0 : 1;
}
