/*
 * hello_server.c
 *
 * シンプルな QUIC サーバー。
 * クライアントから最初の双方向ストリームを受け取ったら
 * "Hello, World!\n" を返して接続を閉じる。
 *
 * OpenSSL 不使用 (picotls minicrypto バックエンドを利用)。
 * H3 / QPACK 不使用。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>   /* AF_INET, AF_INET6 */

#include <picoquic.h>
#include <picoquic_utils.h>
#include <picosocks.h>
#include <picoquic_packet_loop.h>

#define HELLO_ALPN "hello-quic"
#define HELLO_WORLD_MSG "Hello, World!\n"

/* ---- サーバー側ストリームコンテキスト ---- */

typedef struct {
    uint64_t stream_id;
    int      hello_sent;
    int      fin_received;
} hello_server_stream_ctx_t;

/* ---- サーバー側接続コンテキスト ---- */

typedef struct {
    hello_server_stream_ctx_t *stream; /* 最初の 1 ストリームのみ追跡 */
    int done;
} hello_server_ctx_t;

/* ---- サーバーコールバック ---- */

static int hello_server_callback(picoquic_cnx_t *cnx,
    uint64_t stream_id, uint8_t *bytes, size_t length,
    picoquic_call_back_event_t fin_or_event,
    void *callback_ctx, void *v_stream_ctx)
{
    hello_server_ctx_t        *srv_ctx    = (hello_server_ctx_t *)callback_ctx;
    hello_server_stream_ctx_t *stream_ctx = (hello_server_stream_ctx_t *)v_stream_ctx;

    /* 新規接続: デフォルトコンテキストから引き継いで個別コンテキストを割り当て */
    if (srv_ctx == NULL ||
        srv_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx)))
    {
        hello_server_ctx_t *new_ctx = (hello_server_ctx_t *)calloc(1, sizeof(hello_server_ctx_t));
        if (!new_ctx) {
            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return -1;
        }
        picoquic_set_callback(cnx, hello_server_callback, new_ctx);
        srv_ctx = new_ctx;
    }

    switch (fin_or_event) {

    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        /* クライアント発の双方向ストリームのみ処理 */
        if (PICOQUIC_IS_CLIENT_STREAM_ID(stream_id) && PICOQUIC_IS_BIDIR_STREAM_ID(stream_id)) {
            if (stream_ctx == NULL) {
                /* 新しいストリームコンテキストを作成 */
                stream_ctx = (hello_server_stream_ctx_t *)calloc(1, sizeof(hello_server_stream_ctx_t));
                if (!stream_ctx) return -1;
                stream_ctx->stream_id = stream_id;
                if (picoquic_set_app_stream_ctx(cnx, stream_id, stream_ctx) != 0) {
                    free(stream_ctx);
                    return -1;
                }
                srv_ctx->stream = stream_ctx;
            }

            /* 受信データをログ出力 */
            if (length > 0) {
                printf("[server] received %zu bytes on stream %" PRIu64 ": %.*s\n",
                       length, stream_id, (int)length, (char *)bytes);
            }

            /* クライアントの FIN を受けたら "Hello, World!\n" を返す */
            if (fin_or_event == picoquic_callback_stream_fin && !stream_ctx->hello_sent) {
                stream_ctx->fin_received = 1;
                int ret = picoquic_mark_active_stream(cnx, stream_id, 1, stream_ctx);
                if (ret != 0) {
                    fprintf(stderr, "[server] mark_active_stream failed: %d\n", ret);
                }
            }
        }
        break;

    case picoquic_callback_prepare_to_send:
        /* ゼロコピー送信 API: "Hello, World!\n" を書き込んで FIN を立てる */
        if (stream_ctx != NULL && !stream_ctx->hello_sent) {
            const char *msg  = HELLO_WORLD_MSG;
            size_t      mlen = strlen(msg);
            size_t      avail = (mlen < length) ? mlen : length;

            uint8_t *buf = picoquic_provide_stream_data_buffer(bytes, avail, 1 /* fin */, 0);
            if (buf) {
                memcpy(buf, msg, avail);
                stream_ctx->hello_sent = 1;
                printf("[server] sent: %s", msg);
            }
        }
        break;

    case picoquic_callback_stream_reset:
    case picoquic_callback_stop_sending:
        if (stream_ctx) {
            picoquic_reset_stream(cnx, stream_id, 0);
        }
        break;

    case picoquic_callback_stateless_reset:
    case picoquic_callback_close:
    case picoquic_callback_application_close:
        printf("[server] connection closed.\n");
        if (srv_ctx) {
            if (srv_ctx->stream) {
                free(srv_ctx->stream);
                srv_ctx->stream = NULL;
            }
            free(srv_ctx);
        }
        picoquic_set_callback(cnx, NULL, NULL);
        break;

    case picoquic_callback_almost_ready:
        printf("[server] connection almost ready.\n");
        break;
    case picoquic_callback_ready:
        printf("[server] connection ready.\n");
        break;

    default:
        break;
    }
    return 0;
}

/* ---- エントリーポイント ---- */

int main(int argc, char *argv[])
{
    int         server_port = 4433;
    const char *cert_file   = "certs/cert.pem";
    const char *key_file    = "certs/key.pem";

    if (argc >= 2) server_port = atoi(argv[1]);
    if (argc >= 3) cert_file   = argv[2];
    if (argc >= 4) key_file    = argv[3];

    printf("Starting Hello-QUIC server on port %d\n", server_port);

    uint64_t current_time = picoquic_current_time();

    picoquic_quic_t *quic = picoquic_create(
        8,          /* max_nb_connections */
        cert_file,
        key_file,
        NULL,       /* cert root (CA) */
        HELLO_ALPN,
        hello_server_callback,
        NULL,       /* default callback ctx: 接続ごとに設定 */
        NULL, NULL, NULL,
        current_time,
        NULL,
        NULL, NULL, 0);

    if (!quic) {
        fprintf(stderr, "Failed to create QUIC context. Check cert/key paths.\n");
        return 1;
    }

    /* デフォルトコールバックコンテキスト (新規接続のテンプレート識別用) */
    hello_server_ctx_t default_ctx = {0};
    picoquic_set_default_callback(quic, hello_server_callback, &default_ctx);

    /*
     * picoquic_packet_loop の引数:
     *   local_af = AF_INET (IPv4 固定)
     *              0 を渡すと AF_INET + AF_INET6 両方を試みるが、
     *              IPv6 非対応の環境ではエラーになるため AF_INET を明示する。
     */
    int ret = picoquic_packet_loop(quic, server_port, AF_INET, 0, 0, 0, NULL, NULL);

    printf("Server exiting, ret=%d\n", ret);
    picoquic_free(quic);
    return (ret == 0) ? 0 : 1;
}
