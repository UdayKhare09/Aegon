#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static void handle_sec(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("sec ok"));
}

int main(void) {
    /* 1. Config init checks */
    aegon_security_config_t cfg;
    aegon_security_config_init(&cfg);

    CHECK_INT_EQ(cfg.struct_size, sizeof(aegon_security_config_t));
    CHECK(cfg.content_type_nosniff == true);
    CHECK_INT_EQ(cfg.frame_options, AEGON_FRAME_DENY);
    CHECK(cfg.xss_protection_disabled == true);
    CHECK_INT_EQ(cfg.referrer_policy, AEGON_REFERRER_STRICT_ORIGIN_WHEN_CROSS_ORIGIN);
    CHECK(cfg.hsts_enabled == true);
    CHECK_INT_EQ(cfg.hsts_max_age_seconds, 31536000);
    CHECK(cfg.hsts_include_subdomains == true);
    CHECK(cfg.hsts_preload == false);
    CHECK(cfg.content_security_policy == NULL);
    CHECK(cfg.csp_report_only == false);
    CHECK(cfg.permissions_policy == NULL);
    CHECK_INT_EQ(cfg.coop, AEGON_COOP_SAME_ORIGIN);
    CHECK_INT_EQ(cfg.coep, AEGON_COEP_DISABLED);
    CHECK_INT_EQ(cfg.corp, AEGON_CORP_SAME_ORIGIN);
    CHECK(cfg.prevent_browser_caching == false);

    /* 2. Error validation */
    CHECK_INT_EQ(aegon_router_use_security_headers(NULL, &cfg), AEGON_ERR_INVALID_ARG);
    CHECK_INT_EQ(aegon_group_use_security_headers(NULL, &cfg), AEGON_ERR_INVALID_ARG);

    aegon_security_config_t bad_cfg = cfg;
    bad_cfg.struct_size = sizeof(aegon_security_config_t) - 1;
    aegon_router_t* r_tmp = aegon_router_create();
    CHECK_INT_EQ(aegon_router_use_security_headers(r_tmp, &bad_cfg), AEGON_ERR_INVALID_ARG);
    aegon_router_destroy(r_tmp);

    /* 3. Server with Security Headers */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    /* Router: default OWASP baseline */
    CHECK_INT_EQ(aegon_router_use_security_headers(router, &cfg), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/default", handle_sec, NULL, NULL), AEGON_OK);

    /* Group: custom enterprise policies */
    aegon_security_config_t custom_cfg;
    aegon_security_config_init(&custom_cfg);
    custom_cfg.frame_options = AEGON_FRAME_SAMEORIGIN;
    custom_cfg.content_security_policy = "default-src 'self'";
    custom_cfg.permissions_policy = "camera=(), microphone=()";
    custom_cfg.prevent_browser_caching = true;

    aegon_group_t* grp = aegon_router_group(router, "/custom");
    CHECK(grp != NULL);
    CHECK_INT_EQ(aegon_group_use_security_headers(grp, &custom_cfg), AEGON_OK);
    CHECK_INT_EQ(aegon_group_get(grp, "/endpoint", handle_sec, NULL, NULL), AEGON_OK);
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

    /* Test Default Headers */
    CHECK(http_exchange(port, "GET /default HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "x-content-type-options: nosniff") != NULL ||
          strstr(resp, "X-Content-Type-Options: nosniff") != NULL);
    CHECK(strstr(resp, "x-frame-options: DENY") != NULL ||
          strstr(resp, "X-Frame-Options: DENY") != NULL);
    CHECK(strstr(resp, "x-xss-protection: 0") != NULL ||
          strstr(resp, "X-XSS-Protection: 0") != NULL);
    CHECK(strstr(resp, "referrer-policy: strict-origin-when-cross-origin") != NULL ||
          strstr(resp, "Referrer-Policy: strict-origin-when-cross-origin") != NULL);
    CHECK(strstr(resp, "strict-transport-security: max-age=31536000; includeSubDomains") != NULL ||
          strstr(resp, "Strict-Transport-Security: max-age=31536000; includeSubDomains") != NULL);
    CHECK(strstr(resp, "cross-origin-opener-policy: same-origin") != NULL ||
          strstr(resp, "Cross-Origin-Opener-Policy: same-origin") != NULL);
    CHECK(strstr(resp, "cross-origin-resource-policy: same-origin") != NULL ||
          strstr(resp, "Cross-Origin-Resource-Policy: same-origin") != NULL);

    /* Test Custom Group Headers */
    CHECK(http_exchange(port, "GET /custom/endpoint HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "x-frame-options: SAMEORIGIN") != NULL ||
          strstr(resp, "X-Frame-Options: SAMEORIGIN") != NULL);
    CHECK(strstr(resp, "content-security-policy: default-src 'self'") != NULL ||
          strstr(resp, "Content-Security-Policy: default-src 'self'") != NULL);
    CHECK(strstr(resp, "permissions-policy: camera=(), microphone=()") != NULL ||
          strstr(resp, "Permissions-Policy: camera=(), microphone=()") != NULL);
    CHECK(strstr(resp, "cache-control: no-cache, no-store, max-age=0, must-revalidate") != NULL ||
          strstr(resp, "Cache-Control: no-cache, no-store, max-age=0, must-revalidate") != NULL);
    CHECK(strstr(resp, "pragma: no-cache") != NULL ||
          strstr(resp, "Pragma: no-cache") != NULL);

    /* Cleanup */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    return 0;
}

