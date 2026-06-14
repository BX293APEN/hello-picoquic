/*
 * hello_client.c
 *
 * シンプルな QUIC クライアント。
 * サーバーに接続し、双方向ストリームで "Hi!\n" を送信し、
 * サーバーから "Hello, World!\n" を受け取ったら接続を閉じる。
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

#define HELLO_ALPN  "hello-quic"
#define HELLO_SNI   "localhost"
#define CLIENT_MSG  "Hi!\n"

/* ---- クライアント側コールバックコンテキスト ---- */

typedef struct {
    picoquic_cnx_t *cnx;
    uint64_t        stream_id;
    int             msg_sent;
    int             done;
} hello_client_ctx_t;

/* ---- クライアントコールバック ---- */

static int hello_client_callback(picoquic_cnx_t *cnx,
    uint64_t stream_id, uint8_t *bytes, size_t length,
    picoquic_call_back_event_t fin_or_event,
    void *callback_ctx, void *v_stream_ctx)
{
    (void)v_stream_ctx;
    hello_client_ctx_t *ctx = (hello_client_ctx_t *)callback_ctx;
    if (!ctx) return -1;

    switch (fin_or_event) {

    case picoquic_callback_ready:
        /* 接続確立 → 双方向ストリームをオープンしてメッセージ送信をスケジュール */
        printf("[client] connection ready. Opening stream...\n");
        ctx->stream_id = picoquic_get_next_local_stream_id(cnx, 0 /* bidir */);
        if (picoquic_mark_active_stream(cnx, ctx->stream_id, 1, NULL) != 0) {
            fprintf(stderr, "[client] mark_active_stream failed\n");
        }
        break;

    case picoquic_callback_prepare_to_send:
        /* ゼロコピー送信 API: "Hi!\n" を書き込んで FIN を立てる */
        if (stream_id == ctx->stream_id && !ctx->msg_sent) {
            const char *msg  = CLIENT_MSG;
            size_t      mlen = strlen(msg);
            size_t      avail = (mlen < length) ? mlen : length;

            uint8_t *buf = picoquic_provide_stream_data_buffer(bytes, avail, 1 /* fin */, 0);
            if (buf) {
                memcpy(buf, msg, avail);
                ctx->msg_sent = 1;
                printf("[client] sent: %s", msg);
            }
        }
        break;

    case picoquic_callback_stream_data:
        /* サーバーからのデータ */
        if (length > 0) {
            printf("[client] received %zu bytes: %.*s\n",
                   length, (int)length, (char *)bytes);
        }
        break;

    case picoquic_callback_stream_fin:
        /* サーバーが FIN → 全データ受信完了。接続を閉じる */
        if (length > 0) {
            printf("[client] received %zu bytes (with fin): %.*s\n",
                   length, (int)length, (char *)bytes);
        }
        printf("[client] stream fin received. Closing connection.\n");
        picoquic_close(cnx, 0);
        ctx->done = 1;
        break;

    case picoquic_callback_stateless_reset:
    case picoquic_callback_close:
    case picoquic_callback_application_close:
        printf("[client] connection closed.\n");
        ctx->done = 1;
        picoquic_set_callback(cnx, NULL, NULL);
        break;

    case picoquic_callback_almost_ready:
        printf("[client] connection almost ready.\n");
        break;

    default:
        break;
    }
    return 0;
}

/* ---- パケットループコールバック ---- */

static int hello_client_loop_cb(picoquic_quic_t *quic,
    picoquic_packet_loop_cb_enum cb_mode,
    void *callback_ctx, void *callback_arg)
{
    (void)quic; (void)callback_arg;
    hello_client_ctx_t *ctx = (hello_client_ctx_t *)callback_ctx;
    if (!ctx) return PICOQUIC_ERROR_UNEXPECTED_ERROR;

    switch (cb_mode) {
    case picoquic_packet_loop_ready:
        printf("[client] packet loop ready.\n");
        break;
    case picoquic_packet_loop_after_receive:
        break;
    case picoquic_packet_loop_after_send:
        if (ctx->done) {
            return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        }
        break;
    case picoquic_packet_loop_port_update:
        break;
    default:
        /* 未知のモードは無視 (将来の API 拡張に対応) */
        break;
    }
    return 0;
}

/* ---- エントリーポイント ---- */

int main(int argc, char *argv[])
{
    const char *server_name = "127.0.0.1";
    int         server_port = 4433;
    const char *sni         = HELLO_SNI;

    if (argc >= 2) server_name = argv[1];
    if (argc >= 3) server_port = atoi(argv[2]);

    printf("Connecting to %s:%d\n", server_name, server_port);

    uint64_t current_time = picoquic_current_time();

    /* クライアント用 QUIC コンテキスト (証明書・鍵は不要) */
    picoquic_quic_t *quic = picoquic_create(
        1,       /* max_nb_connections */
        NULL,    /* cert (クライアントは不要) */
        NULL,    /* key  (クライアントは不要) */
        NULL,    /* CA root */
        HELLO_ALPN,
        NULL,    /* default callback (接続ごとに設定) */
        NULL,
        NULL, NULL, NULL,
        current_time,
        NULL,
        NULL, NULL, 0);

    if (!quic) {
        fprintf(stderr, "Failed to create QUIC context.\n");
        return 1;
    }

    /* テスト用自己署名証明書への対応: 証明書検証を無効化 */
    picoquic_set_null_verifier(quic);

    /* サーバーアドレスを解決 */
    struct sockaddr_storage server_addr;
    int is_name = 0;
    if (picoquic_get_server_address(server_name, server_port, &server_addr, &is_name) != 0) {
        fprintf(stderr, "Cannot resolve server address: %s\n", server_name);
        picoquic_free(quic);
        return 1;
    }
    if (is_name) sni = server_name;

    /* クライアントコンテキスト */
    hello_client_ctx_t client_ctx = {0};

    /* QUIC 接続を作成 */
    picoquic_cnx_t *cnx = picoquic_create_cnx(
        quic,
        picoquic_null_connection_id,
        picoquic_null_connection_id,
        (struct sockaddr *)&server_addr,
        current_time,
        0,          /* preferred version (0 = デフォルト) */
        sni,
        HELLO_ALPN,
        1           /* client_mode = 1 */
    );

    if (!cnx) {
        fprintf(stderr, "Failed to create connection.\n");
        picoquic_free(quic);
        return 1;
    }

    client_ctx.cnx = cnx;
    picoquic_set_callback(cnx, hello_client_callback, &client_ctx);

    /* 接続開始 */
    if (picoquic_start_client_cnx(cnx) != 0) {
        fprintf(stderr, "Failed to start client connection.\n");
        picoquic_free(quic);
        return 1;
    }

    /*
     * パケットループ:
     *   local_af = server_addr.ss_family (AF_INET) を明示し、
     *              IPv6 非対応環境でのエラーを回避する。
     */
    int ret = picoquic_packet_loop(
        quic,
        0,                        /* local port (0 = OS が割り当て) */
        server_addr.ss_family,    /* AF_INET or AF_INET6 */
        0, 0, 0,
        hello_client_loop_cb,
        &client_ctx);

    printf("Client exiting, ret=%d\n", ret);
    picoquic_free(quic);
    return (ret == 0) ? 0 : 1;
}
