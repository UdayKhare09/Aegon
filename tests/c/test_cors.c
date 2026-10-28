#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static void handle_cors_endpoint(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("cors-ok"));
}

int main(void) {
    /* 1. Config init checks */
    aegon_cors_config_t cfg;
    aegon_cors_config_init(&cfg);

    CHECK_INT_EQ(cfg.struct_size, sizeof(aegon_cors_config_t));
    CHECK_INT_EQ(cfg.allowed_methods, AEGON_METHODS_STANDARD);
    CHECK_INT_EQ(cfg.max_age_seconds, 86400);
    CHECK(cfg.vary_header == true);
    CHECK(cfg.allow_credentials == false);
    CHECK(cfg.allow_private_network == false);
    CHECK(cfg.allowed_origins == NULL);
    CHECK_INT_EQ(cfg.allowed_origins_count, 0);

    /* 2. Error handling */
    CHECK_INT_EQ(aegon_router_use_cors(NULL, &cfg), AEGON_ERR_INVALID_ARG);
    CHECK_INT_EQ(aegon_group_use_cors(NULL, &cfg), AEGON_ERR_INVALID_ARG);

    aegon_cors_config_t bad_cfg = cfg;
    bad_cfg.struct_size = sizeof(aegon_cors_config_t) - 4;
    aegon_router_t* r_tmp = aegon_router_create();
    CHECK_INT_EQ(aegon_router_use_cors(r_tmp, &bad_cfg), AEGON_ERR_INVALID_ARG);
    aegon_router_destroy(r_tmp);

    /* 3. Server 1: Permissive Router-Level CORS */
    {
        uint16_t port = get_ephemeral_port();
        aegon_router_t* router = aegon_router_create();
        CHECK(router != NULL);

        /* Use default/permissive CORS on router */
        CHECK_INT_EQ(aegon_router_use_cors(router, NULL), AEGON_OK);
        CHECK_INT_EQ(aegon_router_get(router, "/public", handle_cors_endpoint, NULL, NULL), AEGON_OK);

        aegon_server_config_t scfg;
        aegon_server_config_init(&scfg);
        aegon_server_t* server = aegon_server_create(&router, &scfg);
        CHECK(server != NULL);
        CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
        CHECK_INT_EQ(aegon_server_run_async(server, 2), AEGON_OK);
        fixture_sleep_ms(50);

        char resp[2048];

        /* Test Permissive Preflight OPTIONS */
        CHECK(http_exchange(port,
            "OPTIONS /public HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Origin: https://client.example.com\r\n"
            "Access-Control-Request-Method: POST\r\n"
            "\r\n",
            resp, sizeof(resp)) > 0);
        CHECK(strstr(resp, "204 No Content") != NULL);
        CHECK(strstr(resp, "access-control-allow-origin: *") != NULL ||
              strstr(resp, "Access-Control-Allow-Origin: *") != NULL);
        CHECK(strstr(resp, "access-control-allow-methods:") != NULL ||
              strstr(resp, "Access-Control-Allow-Methods:") != NULL);

        /* Test Permissive GET Request with Origin */
        CHECK(http_exchange(port,
            "GET /public HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Origin: https://client.example.com\r\n"
            "\r\n",
            resp, sizeof(resp)) > 0);
        CHECK(strstr(resp, "200 OK") != NULL);
        CHECK(strstr(resp, "cors-ok") != NULL);
        CHECK(strstr(resp, "access-control-allow-origin: *") != NULL ||
              strstr(resp, "Access-Control-Allow-Origin: *") != NULL);

        aegon_server_stop(server);
        CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
        aegon_server_destroy(server);
    }

    /* 4. Server 2: Strict Group-Level CORS with Credentials */
    {
        uint16_t port = get_ephemeral_port();
        aegon_router_t* router = aegon_router_create();
        CHECK(router != NULL);

        const char* strict_origins[] = { "https://trusted.app.com" };
        aegon_cors_config_t strict_cfg;
        aegon_cors_config_init(&strict_cfg);
        strict_cfg.allowed_origins = strict_origins;
        strict_cfg.allowed_origins_count = 1;
        strict_cfg.allow_credentials = true;

        aegon_group_t* grp = aegon_router_group(router, "/strict");
        CHECK(grp != NULL);
        CHECK_INT_EQ(aegon_group_use_cors(grp, &strict_cfg), AEGON_OK);
        CHECK_INT_EQ(aegon_group_all(grp, "/data", handle_cors_endpoint, NULL, NULL), AEGON_OK);
        aegon_group_destroy(grp);

        aegon_server_config_t scfg;
        aegon_server_config_init(&scfg);
        aegon_server_t* server = aegon_server_create(&router, &scfg);
        CHECK(server != NULL);
        CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
        CHECK_INT_EQ(aegon_server_run_async(server, 2), AEGON_OK);
        fixture_sleep_ms(50);

        char resp[2048];

        /* Test Strict Group Preflight - Allowed Origin */
        CHECK(http_exchange(port,
            "OPTIONS /strict/data HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Origin: https://trusted.app.com\r\n"
            "Access-Control-Request-Method: GET\r\n"
            "\r\n",
            resp, sizeof(resp)) > 0);
        CHECK(strstr(resp, "204 No Content") != NULL);
        CHECK(strstr(resp, "https://trusted.app.com") != NULL);
        CHECK(strstr(resp, "access-control-allow-credentials: true") != NULL ||
              strstr(resp, "Access-Control-Allow-Credentials: true") != NULL);

        /* Test Strict Group Actual GET - Allowed Origin */
        CHECK(http_exchange(port,
            "GET /strict/data HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Origin: https://trusted.app.com\r\n"
            "\r\n",
            resp, sizeof(resp)) > 0);
        CHECK(strstr(resp, "200 OK") != NULL);
        CHECK(strstr(resp, "cors-ok") != NULL);
        CHECK(strstr(resp, "https://trusted.app.com") != NULL);
        CHECK(strstr(resp, "access-control-allow-credentials: true") != NULL ||
              strstr(resp, "Access-Control-Allow-Credentials: true") != NULL);

        /* Test Strict Group Preflight - Disallowed Origin */
        CHECK(http_exchange(port,
            "OPTIONS /strict/data HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Origin: https://untrusted.evil.com\r\n"
            "Access-Control-Request-Method: GET\r\n"
            "\r\n",
            resp, sizeof(resp)) > 0);
        /* Should not allow untrusted origin */
        CHECK(strstr(resp, "https://untrusted.evil.com") == NULL);

        aegon_server_stop(server);
        CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
        aegon_server_destroy(server);
    }

    return 0;
}
