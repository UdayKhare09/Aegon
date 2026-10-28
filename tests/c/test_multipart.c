#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static int g_upload_handled = 0;

static void handle_upload(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    g_upload_handled++;

    aegon_multipart_t* mp = aegon_multipart_parse(ctx);
    CHECK(mp != NULL);

    /* 1. Fields check */
    CHECK_INT_EQ(aegon_multipart_field_count(mp), 2);

    aegon_str_t user = aegon_multipart_field(mp, "username");
    CHECK_INT_EQ(user.len, 5);
    CHECK(strncmp(user.data, "admin", 5) == 0);

    aegon_str_t role = aegon_multipart_field(mp, "role");
    CHECK_INT_EQ(role.len, 9);
    CHECK(strncmp(role.data, "superuser", 9) == 0);

    aegon_str_t none = aegon_multipart_field(mp, "not_here");
    CHECK(none.data == NULL);
    CHECK_INT_EQ(none.len, 0);

    /* Test indexed field iteration */
    aegon_str_t fn1, fv1;
    CHECK(aegon_multipart_field_at(mp, 0, &fn1, &fv1));
    CHECK_INT_EQ(fn1.len, 8);
    CHECK(strncmp(fn1.data, "username", 8) == 0);
    CHECK_INT_EQ(fv1.len, 5);
    CHECK(strncmp(fv1.data, "admin", 5) == 0);

    /* 2. Files check */
    CHECK_INT_EQ(aegon_multipart_file_count(mp), 1);

    aegon_multipart_file_info_t finfo;
    CHECK(aegon_multipart_file(mp, "avatar", &finfo));
    CHECK_INT_EQ(finfo.name.len, 6);
    CHECK(strncmp(finfo.name.data, "avatar", 6) == 0);
    CHECK_INT_EQ(finfo.filename.len, 10);
    CHECK(strncmp(finfo.filename.data, "avatar.png", 10) == 0);
    CHECK_INT_EQ(finfo.content_type.len, 9);
    CHECK(strncmp(finfo.content_type.data, "image/png", 9) == 0);
    CHECK_INT_EQ(finfo.data.len, 4);
    CHECK(memcmp(finfo.data.data, "\x89PNG", 4) == 0);

    /* Indexed file iteration */
    aegon_multipart_file_info_t finfo_idx;
    CHECK(aegon_multipart_file_at(mp, 0, &finfo_idx));
    CHECK_INT_EQ(finfo_idx.data.len, 4);

    CHECK(!aegon_multipart_file_at(mp, 1, &finfo_idx));

    aegon_multipart_destroy(mp);

    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("upload ok"));
}

static void handle_bad_multipart(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_multipart_t* mp = aegon_multipart_parse(ctx);
    /* Should fail to parse */
    CHECK(mp == NULL);

    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_BAD_REQUEST);
    aegon_res_text(res, AEGON_STR("invalid multipart"));
}

int main(void) {
    /* 1. Null handling */
    CHECK(aegon_multipart_parse(NULL) == NULL);
    aegon_multipart_destroy(NULL); /* Safe no-op */
    CHECK_INT_EQ(aegon_multipart_field_count(NULL), 0);
    CHECK_INT_EQ(aegon_multipart_file_count(NULL), 0);
    CHECK(!aegon_multipart_field_at(NULL, 0, NULL, NULL));
    CHECK(!aegon_multipart_file_at(NULL, 0, NULL));

    /* 2. Setup server */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    CHECK_INT_EQ(aegon_router_post(router, "/upload", handle_upload, NULL, NULL), AEGON_OK);
    CHECK_INT_EQ(aegon_router_post(router, "/bad_upload", handle_bad_multipart, NULL, NULL), AEGON_OK);

    aegon_server_config_t scfg;
    aegon_server_config_init(&scfg);
    aegon_server_t* server = aegon_server_create(&router, &scfg);
    CHECK(server != NULL);
    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 2), AEGON_OK);
    fixture_sleep_ms(50);

    /* Construct multipart payload */
    const char part1[] =
        "-----------------------------974767299852498929531610575\r\n"
        "Content-Disposition: form-data; name=\"username\"\r\n\r\n"
        "admin\r\n"
        "-----------------------------974767299852498929531610575\r\n"
        "Content-Disposition: form-data; name=\"role\"\r\n\r\n"
        "superuser\r\n"
        "-----------------------------974767299852498929531610575\r\n"
        "Content-Disposition: form-data; name=\"avatar\"; filename=\"avatar.png\"\r\n"
        "Content-Type: image/png\r\n\r\n"
        "\x89PNG\r\n"
        "-----------------------------974767299852498929531610575--\r\n";

    size_t body_len = strlen(part1);
    char http_req[2048];
    int n = snprintf(http_req, sizeof(http_req),
        "POST /upload HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: multipart/form-data; boundary=---------------------------974767299852498929531610575\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        body_len);
    CHECK(n > 0);
    memcpy(http_req + n, part1, body_len);
    http_req[n + body_len] = '\0';

    char resp[1024];
    CHECK(http_exchange(port, http_req, resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "upload ok") != NULL);
    CHECK_INT_EQ(g_upload_handled, 1);

    /* Test Bad Multipart (plain text content-type) */
    const char bad_req[] =
        "POST /bad_upload HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "test";
    CHECK(http_exchange(port, bad_req, resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "400 Bad Request") != NULL);
    CHECK(strstr(resp, "invalid multipart") != NULL);

    /* Cleanup */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    return 0;
}
