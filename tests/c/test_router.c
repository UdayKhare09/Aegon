#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static int g_cleanup_called = 0;
static void test_cleanup(void* ud) {
    int* p = (int*)ud;
    if (p) (*p)++;
}

static void handle_static(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_text(aegon_ctx_res(ctx), AEGON_STR("static match"));
}

static void handle_users(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_str_t id = aegon_req_param(req, AEGON_STR("id"));
    aegon_res_text(aegon_ctx_res(ctx), id);
}

static void handle_files(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_str_t wildcard = aegon_req_param(req, AEGON_STR("filepath"));
    aegon_res_text(aegon_ctx_res(ctx), wildcard);
}

static void handle_custom_404(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_NOT_FOUND);
    aegon_res_text(res, AEGON_STR("Custom 404 Page"));
}

static void handle_custom_405(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_METHOD_NOT_ALLOWED);
    aegon_res_text(res, AEGON_STR("Custom 405 Not Allowed"));
}

int main(void) {
    /* 1. Test router cleanup callback invocation */
    {
        aegon_router_t* r_tmp = aegon_router_create();
        CHECK(r_tmp != NULL);
        CHECK_INT_EQ(aegon_router_get(r_tmp, "/test_clean", handle_static, &g_cleanup_called, test_cleanup), AEGON_OK);
        CHECK_INT_EQ(g_cleanup_called, 0);
        aegon_router_destroy(r_tmp);
        CHECK_INT_EQ(g_cleanup_called, 1);
    }

    /* 2. Setup server for routing tests */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    CHECK_INT_EQ(aegon_router_get(router, "/static", handle_static, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/users/:id", handle_users, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/files/*filepath", handle_files, NULL, NULL), AEGON_OK);

    CHECK_INT_EQ(aegon_router_on_not_found(router, handle_custom_404, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_on_method_not_allowed(router, handle_custom_405, NULL, NULL), AEGON_OK);

    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);

    aegon_server_t* server = aegon_server_create(&router, &cfg);
    CHECK(server != NULL);
    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 1), AEGON_OK);

    fixture_sleep_ms(50);
    CHECK(aegon_server_is_running(server));

    char resp[2048];

    /* Exact static route hit */
    CHECK(http_exchange(port, "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "static match") != NULL);

    /* Trailing slash normalization */
    CHECK(http_exchange(port, "GET /static/ HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "static match") != NULL);

    /* Parameter extraction */
    CHECK(http_exchange(port, "GET /users/12345 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "12345") != NULL);

    /* Wildcard extraction */
    CHECK(http_exchange(port, "GET /files/docs/readme.txt HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "docs/readme.txt") != NULL);

    /* Custom 404 */
    CHECK(http_exchange(port, "GET /no_such_route HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "404 Not Found") != NULL);
    CHECK(strstr(resp, "Custom 404 Page") != NULL);

    /* Custom 405 (PUT on GET-only route) */
    CHECK(http_exchange(port, "PUT /static HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "405 Method Not Allowed") != NULL);
    CHECK(strstr(resp, "Custom 405 Not Allowed") != NULL);

    /* Teardown */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    return 0;
}
