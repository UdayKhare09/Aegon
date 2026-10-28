#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static int g_custom_formatter_called = 0;
static int g_skip_if_called = 0;
static uint64_t g_last_duration_ns = 0;

static bool access_log_skip(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    g_skip_if_called++;
    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_str_t path = aegon_req_path(req);
    if (path.len >= 5 && strncmp(path.data, "/skip", 5) == 0) {
        return true; /* skip */
    }
    return false;
}

static size_t access_log_custom_fmt(aegon_ctx_t* ctx, uint64_t duration_ns,
                                    char* buf, size_t buf_cap, void* ud) {
    (void)ud;
    g_custom_formatter_called++;
    g_last_duration_ns = duration_ns;

    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_str_t path = aegon_req_path(req);

    int n = snprintf(buf, buf_cap, "CUSTOM_ACCESS_LOG path=%.*s duration=%llu",
                     (int)path.len, path.data, (unsigned long long)duration_ns);
    if (n < 0 || (size_t)n >= buf_cap) return 0;
    return (size_t)n;
}

static void handle_access(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("access ok"));
}

int main(void) {
    /* 1. Config init checks */
    aegon_access_log_config_t cfg;
    aegon_access_log_config_init(&cfg);

    CHECK_INT_EQ(cfg.struct_size, sizeof(aegon_access_log_config_t));
    CHECK_INT_EQ(cfg.format, AEGON_ACCESS_LOG_COMBINED);
    CHECK_INT_EQ(cfg.normal_level, AEGON_LOG_INFO);
    CHECK_INT_EQ(cfg.client_error_level, AEGON_LOG_WARN);
    CHECK_INT_EQ(cfg.server_error_level, AEGON_LOG_ERROR);
    CHECK_INT_EQ(cfg.slow_threshold_ns, 0);
    CHECK_INT_EQ(cfg.slow_level, AEGON_LOG_WARN);
    CHECK(cfg.log_query == true);
    CHECK(cfg.log_client_ip == true);
    CHECK(cfg.log_bytes == true);
    CHECK(cfg.log_protocol == true);
    CHECK_STR_EQ(cfg.correlation_header, "x-request-id");
    CHECK(cfg.skip_if == NULL);
    CHECK(cfg.custom_formatter == NULL);

    /* 2. Error validation */
    CHECK_INT_EQ(aegon_router_use_access_log(NULL, &cfg), AEGON_ERR_INVALID_ARG);
    CHECK_INT_EQ(aegon_group_use_access_log(NULL, &cfg), AEGON_ERR_INVALID_ARG);

    aegon_access_log_config_t bad_cfg = cfg;
    bad_cfg.struct_size = sizeof(aegon_access_log_config_t) + 4;
    aegon_router_t* r_tmp = aegon_router_create();
    CHECK_INT_EQ(aegon_router_use_access_log(r_tmp, &bad_cfg), AEGON_ERR_INVALID_ARG);
    aegon_router_destroy(r_tmp);

    /* 3. Server with custom access log */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    cfg.format = AEGON_ACCESS_LOG_CUSTOM;
    cfg.skip_if = access_log_skip;
    cfg.custom_formatter = access_log_custom_fmt;

    CHECK_INT_EQ(aegon_router_use_access_log(router, &cfg), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/logged", handle_access, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/skip/health", handle_access, NULL, NULL), AEGON_OK);

    /* Group with access log */
    aegon_access_log_config_t grp_cfg;
    aegon_access_log_config_init(&grp_cfg);
    aegon_group_t* grp = aegon_router_group(router, "/v1");
    CHECK(grp != NULL);
    CHECK_INT_EQ(aegon_group_use_access_log(grp, &grp_cfg), AEGON_OK);
    CHECK_INT_EQ(aegon_group_get(grp, "/status", handle_access, NULL, NULL), AEGON_OK);
    aegon_group_destroy(grp);

    /* Start server */
    aegon_server_config_t scfg;
    aegon_server_config_init(&scfg);
    aegon_server_t* server = aegon_server_create(&router, &scfg);
    CHECK(server != NULL);
    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 2), AEGON_OK);
    fixture_sleep_ms(50);

    char resp[1024];

    /* Test Logged Endpoint */
    CHECK(http_exchange(port, "GET /logged HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "access ok") != NULL);
    CHECK_INT_EQ(g_skip_if_called, 1);
    CHECK_INT_EQ(g_custom_formatter_called, 1);
    CHECK(g_last_duration_ns > 0);

    /* Test Skipped Endpoint */
    CHECK(http_exchange(port, "GET /skip/health HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK_INT_EQ(g_skip_if_called, 2);
    /* Formatter should NOT have been called for skipped request */
    CHECK_INT_EQ(g_custom_formatter_called, 1);

    /* Test Group Access Log */
    CHECK(http_exchange(port, "GET /v1/status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);

    /* Cleanup */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    return 0;
}
