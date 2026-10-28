#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static void handle_text(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("Hello plain text"));
}

static void handle_json(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_json(res, AEGON_STR("{\"hello\":\"world\"}"));
}

static void handle_html(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_html(res, AEGON_STR("<h1>Aegon HTML</h1>"));
}

static void handle_cookie(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_cookie_options_t opt;
    aegon_cookie_options_init(&opt);
    opt.name = AEGON_STR("auth_token");
    opt.value = AEGON_STR("secret123");
    opt.path = AEGON_STR("/");
    opt.max_age_seconds = 3600;
    opt.http_only = true;
    opt.secure = true;
    opt.same_site = AEGON_SAMESITE_LAX;

    CHECK_INT_EQ(aegon_res_set_cookie(res, &opt), AEGON_OK);
    aegon_res_text(res, AEGON_STR("cookie ok"));
}

static void handle_redirect(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    CHECK_INT_EQ(aegon_res_redirect(res, AEGON_STATUS_SEE_OTHER, AEGON_STR("/destination")), AEGON_OK);
}

static void handle_static(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_header_static(res, AEGON_STR("X-Static-Header"), AEGON_STR("static-val"));
    aegon_res_body_static(res, AEGON_STR("static-content"));
}

static void handle_problem(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    CHECK_INT_EQ(aegon_ctx_problem(ctx, AEGON_STATUS_BAD_REQUEST,
                                   AEGON_STR("Invalid Request"),
                                   AEGON_STR("Missing parameter")), AEGON_OK);
}

int main(void) {
    /* 1. Test mime_from_extension */
    CHECK(strstr(aegon_mime_from_extension("json"), "application/json") != NULL);
    CHECK(strstr(aegon_mime_from_extension("html"), "text/html") != NULL);
    CHECK(strstr(aegon_mime_from_extension("txt"), "text/plain") != NULL);
    CHECK(strstr(aegon_mime_from_extension("js"), "application/javascript") != NULL);
    CHECK_STR_EQ(aegon_mime_from_extension("png"), "image/png");
    CHECK_STR_EQ(aegon_mime_from_extension("nonexistent_extension_xyz"), "application/octet-stream");

    /* 2. Setup server with various response routes */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    CHECK_INT_EQ(aegon_router_get(router, "/text", handle_text, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/json", handle_json, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/html", handle_html, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/cookie", handle_cookie, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/redirect", handle_redirect, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/static", handle_static, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_get(router, "/problem", handle_problem, NULL, NULL), AEGON_OK);

    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);

    aegon_server_t* server = aegon_server_create(&router, &cfg);
    CHECK(server != NULL);
    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 1), AEGON_OK);

    fixture_sleep_ms(50);
    CHECK(aegon_server_is_running(server));

    char resp[2048];

    /* Test /text */
    CHECK(http_exchange(port, "GET /text HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "text/plain") != NULL);
    CHECK(strstr(resp, "Hello plain text") != NULL);

    /* Test /json */
    CHECK(http_exchange(port, "GET /json HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "application/json") != NULL);
    CHECK(strstr(resp, "{\"hello\":\"world\"}") != NULL);

    /* Test /html */
    CHECK(http_exchange(port, "GET /html HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "text/html") != NULL);
    CHECK(strstr(resp, "<h1>Aegon HTML</h1>") != NULL);

    /* Test /cookie */
    CHECK(http_exchange(port, "GET /cookie HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "Set-Cookie: auth_token=secret123") != NULL);
    CHECK(strstr(resp, "Path=/") != NULL);
    CHECK(strstr(resp, "HttpOnly") != NULL);

    /* Test /redirect */
    CHECK(http_exchange(port, "GET /redirect HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "303") != NULL);
    CHECK(strstr(resp, "Location: /destination") != NULL);

    /* Test /static */
    CHECK(http_exchange(port, "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "X-Static-Header: static-val") != NULL);
    CHECK(strstr(resp, "static-content") != NULL);

    /* Test /problem */
    CHECK(http_exchange(port, "GET /problem HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "400 Bad Request") != NULL);
    CHECK(strstr(resp, "application/problem+json") != NULL);
    CHECK(strstr(resp, "Invalid Request") != NULL);
    CHECK(strstr(resp, "Missing parameter") != NULL);

    /* Teardown */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    return 0;
}
