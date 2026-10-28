#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static int g_handler_called = 0;

static void handle_request_test(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    g_handler_called++;
    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_res_t* res = aegon_ctx_res(ctx);

    /* Method & Version */
    CHECK_INT_EQ(aegon_req_method(req), AEGON_METHOD_POST);
    CHECK_INT_EQ(aegon_req_version(req), AEGON_HTTP_1_1);

    /* Path & Query String */
    aegon_str_t path = aegon_req_path(req);
    CHECK_INT_EQ(path.len, 12);
    CHECK(memcmp(path.data, "/test/req/42", 12) == 0);

    aegon_str_t qs = aegon_req_query_string(req);
    CHECK(qs.len > 0);
    CHECK(memcmp(qs.data, "filter=active&sort=desc", 23) == 0);

    /* Query param lookup */
    aegon_str_t q_filter = aegon_req_query(req, AEGON_STR("filter"));
    CHECK_INT_EQ(q_filter.len, 6);
    CHECK(memcmp(q_filter.data, "active", 6) == 0);

    aegon_str_t q_sort = aegon_req_query(req, AEGON_STR("sort"));
    CHECK_INT_EQ(q_sort.len, 4);
    CHECK(memcmp(q_sort.data, "desc", 4) == 0);

    aegon_str_t q_missing = aegon_req_query(req, AEGON_STR("notfound"));
    CHECK(q_missing.data == NULL);

    /* Body */
    aegon_str_t body = aegon_req_body(req);
    CHECK_INT_EQ(body.len, 12);
    CHECK(memcmp(body.data, "test payload", 12) == 0);

    /* Headers */
    CHECK(aegon_req_has_header(req, AEGON_STR("X-Custom")));
    CHECK(!aegon_req_has_header(req, AEGON_STR("X-Non-Existent")));

    aegon_str_t h_custom = aegon_req_header(req, AEGON_STR("X-Custom"));
    CHECK_INT_EQ(h_custom.len, 13);
    CHECK(memcmp(h_custom.data, "my-header-val", 13) == 0);

    /* Header iteration */
    size_t h_count = aegon_req_header_count(req);
    CHECK(h_count >= 3);
    bool found_custom = false;
    for (size_t i = 0; i < h_count; ++i) {
        aegon_str_t k, v;
        if (aegon_req_header_at(req, i, &k, &v)) {
            if (k.len == 8 && (memcmp(k.data, "x-custom", 8) == 0 || memcmp(k.data, "X-Custom", 8) == 0)) {
                found_custom = true;
                CHECK(v.len == 13 && memcmp(v.data, "my-header-val", 13) == 0);
            }
        }
    }
    CHECK(found_custom);

    /* Route params */
    aegon_str_t p_id = aegon_req_param(req, AEGON_STR("id"));
    CHECK_INT_EQ(p_id.len, 2);
    CHECK(memcmp(p_id.data, "42", 2) == 0);

    CHECK_INT_EQ(aegon_req_param_count(req), 1);
    aegon_str_t pk, pv;
    CHECK(aegon_req_param_at(req, 0, &pk, &pv));
    CHECK(pk.len == 2 && memcmp(pk.data, "id", 2) == 0);
    CHECK(pv.len == 2 && memcmp(pv.data, "42", 2) == 0);

    /* Cookie */
    aegon_str_t cookie = aegon_req_cookie(req, AEGON_STR("session"));
    CHECK_INT_EQ(cookie.len, 6);
    CHECK(memcmp(cookie.data, "xyz123", 6) == 0);

    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("OK"));
}

int main(void) {
    /* 1. Test url_decode standalone */
    aegon_str_t dec1 = aegon_url_decode(AEGON_STR("hello%20world"));
    CHECK(dec1.data != NULL);
    CHECK_INT_EQ(dec1.len, 11);
    CHECK(memcmp(dec1.data, "hello world", 11) == 0);
    aegon_mem_free((void*)dec1.data);

    aegon_str_t dec2 = aegon_url_decode(AEGON_STR("a%2Bb%20c"));
    CHECK(dec2.data != NULL);
    CHECK_INT_EQ(dec2.len, 5);
    CHECK(memcmp(dec2.data, "a+b c", 5) == 0);
    aegon_mem_free((void*)dec2.data);

    /* 2. Setup server with route */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);
    CHECK_INT_EQ(aegon_router_post(router, "/test/req/:id", handle_request_test, NULL, NULL), AEGON_OK);

    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);

    aegon_server_t* server = aegon_server_create(&router, &cfg);
    CHECK(server != NULL);
    CHECK(router == NULL); /* Router must be consumed */

    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 1), AEGON_OK);

    fixture_sleep_ms(50);
    CHECK(aegon_server_is_running(server));

    /* 3. Send HTTP request */
    const char* http_req =
        "POST /test/req/42?filter=active&sort=desc HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "X-Custom: my-header-val\r\n"
        "Cookie: session=xyz123\r\n"
        "Content-Length: 12\r\n"
        "\r\n"
        "test payload";

    char resp_buf[1024];
    int n = http_exchange(port, http_req, resp_buf, sizeof(resp_buf));
    CHECK(n > 0);
    CHECK(strstr(resp_buf, "200 OK") != NULL);
    CHECK(strstr(resp_buf, "OK") != NULL);
    CHECK_INT_EQ(g_handler_called, 1);

    /* 4. Teardown */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    return 0;
}
