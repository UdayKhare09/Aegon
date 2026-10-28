#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static void* const EXPECTED_APP_DATA = (void*)(uintptr_t)0xbeef1234;
static int g_server_handler_called = 0;

static void handle_server_test(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    g_server_handler_called++;

    /* 1. App data check */
    void* app_data = aegon_ctx_app_data(ctx);
    CHECK(app_data == EXPECTED_APP_DATA);

    /* 2. Per-request locals check */
    CHECK(aegon_ctx_local_get(ctx, "session_id") == NULL);
    CHECK_INT_EQ(aegon_ctx_local_set(ctx, "session_id", (void*)(uintptr_t)0xcafe), AEGON_OK);
    CHECK(aegon_ctx_local_get(ctx, "session_id") == (void*)(uintptr_t)0xcafe);

    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("server test ok"));
}

int main(void) {
    /* 1. Test config init defaults */
    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);

    CHECK_INT_EQ(cfg.struct_size, sizeof(aegon_server_config_t));
    CHECK_INT_EQ(cfg.engine, AEGON_ENGINE_AUTO);
    CHECK_INT_EQ(cfg.ring_entries, 4096);
    CHECK_INT_EQ(cfg.buffer_pool_entries, 8192);
    CHECK_INT_EQ(cfg.buffer_size, 4096);
    CHECK_INT_EQ(cfg.max_uri_length, 8192);
    CHECK_INT_EQ(cfg.max_headers_size, 65536);
    CHECK_INT_EQ(cfg.max_header_count, 100);
    CHECK_INT_EQ(cfg.max_body_size, 16 * 1024 * 1024);
    CHECK(cfg.tcp_nodelay == true);
    CHECK(cfg.tcp_keepalive == true);
    CHECK_INT_EQ(cfg.tcp_keepidle, 30);
    CHECK_INT_EQ(cfg.tcp_keepintvl, 10);
    CHECK_INT_EQ(cfg.tcp_keepcnt, 3);
    CHECK_INT_EQ(cfg.h2_max_concurrent_streams, 256);
    CHECK_INT_EQ(cfg.h2_initial_window_size, 1048576);
    CHECK_INT_EQ(cfg.rst_burst_limit, 1000);
    CHECK_INT_EQ(cfg.ws_max_message_size, 16 * 1024 * 1024);
    CHECK_INT_EQ(cfg.ws_max_frame_size, 16 * 1024 * 1024);
    CHECK(cfg.ws_require_masked_frames == true);
    CHECK_INT_EQ(cfg.ws_auto_ping_interval_sec, 0);
    CHECK_INT_EQ(cfg.ws_ping_timeout_sec, 10);
    CHECK_INT_EQ(cfg.ws_initial_buffer_capacity, 4096);
    CHECK(cfg.enable_http3 == true);

    /* 2. Error cases */
    CHECK(aegon_server_create(NULL, &cfg) == NULL);

    aegon_router_t* r_bad = aegon_router_create();
    aegon_server_t* s_bad = aegon_server_create(&r_bad, &cfg);
    CHECK(s_bad != NULL);
    CHECK(r_bad == NULL);

    /* Invalid port */
    CHECK_INT_EQ(aegon_server_listen(s_bad, 0, "127.0.0.1"), AEGON_ERR_INVALID_ARG);

    /* Run without listener -> AEGON_ERR_STATE */
    CHECK_INT_EQ(aegon_server_run(s_bad, 1), AEGON_ERR_STATE);
    CHECK_INT_EQ(aegon_server_run_async(s_bad, 1), AEGON_ERR_STATE);

    aegon_server_destroy(s_bad);

    /* 3. Server with app_data and async run */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);
    CHECK_INT_EQ(aegon_router_get(router, "/server_test", handle_server_test, NULL, NULL), AEGON_OK);

    aegon_server_t* server = aegon_server_create(&router, &cfg);
    CHECK(server != NULL);

    CHECK_INT_EQ(aegon_server_set_app_data(server, EXPECTED_APP_DATA), AEGON_OK);
    CHECK(aegon_server_app_data(server) == EXPECTED_APP_DATA);

    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 2), AEGON_OK);

    fixture_sleep_ms(50);
    CHECK(aegon_server_is_running(server));

    char resp[1024];
    CHECK(http_exchange(port, "GET /server_test HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "server test ok") != NULL);
    CHECK_INT_EQ(g_server_handler_called, 1);

    /* 4. Graceful stop and join */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    CHECK(!aegon_server_is_running(server));

    aegon_server_destroy(server);

    return 0;
}

