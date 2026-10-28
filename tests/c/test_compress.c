#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static const char LARGE_PAYLOAD[] =
    "Aegon enterprise HTTP framework high-performance async coroutine C ABI compression test. "
    "Repeating string to easily exceed the 256-byte threshold for compression middleware. "
    "Aegon enterprise HTTP framework high-performance async coroutine C ABI compression test. "
    "Repeating string to easily exceed the 256-byte threshold for compression middleware. "
    "Aegon enterprise HTTP framework high-performance async coroutine C ABI compression test. "
    "Repeating string to easily exceed the 256-byte threshold for compression middleware. ";

static void handle_large(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR(LARGE_PAYLOAD));
}

static void handle_small(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("short text"));
}

int main(void) {
    /* 1. Config init checks */
    aegon_compress_config_t cfg;
    aegon_compress_config_init(&cfg);

    CHECK_INT_EQ(cfg.struct_size, sizeof(aegon_compress_config_t));
    CHECK_INT_EQ(cfg.level, 1);
    CHECK_INT_EQ(cfg.min_size, 256);
    CHECK(cfg.gzip == true);
    CHECK(cfg.deflate == true);
    CHECK(cfg.prefer_deflate == true);
    CHECK(cfg.vary == true);

    /* 2. Error validation */
    CHECK_INT_EQ(aegon_router_use_compress(NULL, &cfg), AEGON_ERR_INVALID_ARG);
    CHECK_INT_EQ(aegon_group_use_compress(NULL, &cfg), AEGON_ERR_INVALID_ARG);

    aegon_compress_config_t bad_cfg = cfg;
    bad_cfg.struct_size = sizeof(aegon_compress_config_t) - 2;
    aegon_router_t* r_tmp = aegon_router_create();
    CHECK_INT_EQ(aegon_router_use_compress(r_tmp, &bad_cfg), AEGON_ERR_INVALID_ARG);
    aegon_router_destroy(r_tmp);

    /* 3. Server with compression */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    /* Router-level default compression */
    CHECK_INT_EQ(aegon_router_use_compress(router, &cfg), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/large", handle_large, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/small", handle_small, NULL, NULL), AEGON_OK);

    /* Group with gzip-only compression */
    aegon_compress_config_t gzip_cfg;
    aegon_compress_config_init(&gzip_cfg);
    gzip_cfg.deflate = false;
    gzip_cfg.prefer_deflate = false;

    aegon_group_t* grp = aegon_router_group(router, "/gzip_only");
    CHECK(grp != NULL);
    CHECK_INT_EQ(aegon_group_use_compress(grp, &gzip_cfg), AEGON_OK);
    CHECK_INT_EQ(aegon_group_get(grp, "/large", handle_large, NULL, NULL), AEGON_OK);
    aegon_group_destroy(grp);

    /* Start server */
    aegon_server_config_t scfg;
    aegon_server_config_init(&scfg);
    aegon_server_t* server = aegon_server_create(&router, &scfg);
    CHECK(server != NULL);
    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 2), AEGON_OK);
    fixture_sleep_ms(50);

    char resp[4096];

    /* Test Large Payload with Accept-Encoding: gzip, deflate (prefers deflate by default) */
    CHECK(http_exchange(port,
        "GET /large HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Accept-Encoding: gzip, deflate\r\n"
        "\r\n",
        resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "content-encoding: deflate") != NULL ||
          strstr(resp, "Content-Encoding: deflate") != NULL);
    CHECK(strstr(resp, "vary:") != NULL || strstr(resp, "Vary:") != NULL);

    /* Test Large Payload without Accept-Encoding */
    CHECK(http_exchange(port,
        "GET /large HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "\r\n",
        resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "content-encoding:") == NULL &&
          strstr(resp, "Content-Encoding:") == NULL);
    CHECK(strstr(resp, "Aegon enterprise HTTP framework") != NULL);

    /* Test Small Payload with Accept-Encoding (below min_size=256) */
    CHECK(http_exchange(port,
        "GET /small HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Accept-Encoding: gzip, deflate\r\n"
        "\r\n",
        resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "content-encoding:") == NULL &&
          strstr(resp, "Content-Encoding:") == NULL);
    CHECK(strstr(resp, "short text") != NULL);

    /* Test Gzip-only Group */
    CHECK(http_exchange(port,
        "GET /gzip_only/large HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Accept-Encoding: gzip, deflate\r\n"
        "\r\n",
        resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "content-encoding: gzip") != NULL ||
          strstr(resp, "Content-Encoding: gzip") != NULL);

    /* Cleanup */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    return 0;
}

