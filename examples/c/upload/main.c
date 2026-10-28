#include <aegon/c/aegon.h>
#include <stdio.h>

static void handle_upload_endpoint(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_multipart_t* mp = aegon_multipart_parse(ctx);
    if (!mp) {
        aegon_ctx_problem(ctx, AEGON_STATUS_BAD_REQUEST,
                          AEGON_STR("Bad Request"),
                          AEGON_STR("Expected valid multipart/form-data payload"));
        return;
    }

    aegon_str_t description = aegon_multipart_field(mp, "description");
    aegon_multipart_file_info_t file_info;
    bool has_file = aegon_multipart_file(mp, "file", &file_info);

    if (has_file) {
        printf("Received upload: filename=%.*s size=%zu bytes description=%.*s\n",
               (int)file_info.filename.len, file_info.filename.data,
               file_info.data.len,
               (int)description.len, description.data ? description.data : "(none)");

        /* In a real application, save to disk or process stream: */
        /* FILE* out = fopen("uploaded.bin", "wb"); ... */
    }

    aegon_multipart_destroy(mp);

    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_json(res, AEGON_STR("{\"uploaded\":true}"));
}

int main(void) {
    aegon_log_init(AEGON_LOG_INFO, false);
    printf("Starting Aegon C ABI Multipart Upload example...\n");

    aegon_router_t* router = aegon_router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router: %s\n", aegon_last_error());
        return 1;
    }

    aegon_router_post(router, "/upload", handle_upload_endpoint, NULL, NULL);

    aegon_server_config_t scfg;
    aegon_server_config_init(&scfg);
    aegon_server_t* server = aegon_server_create(&router, &scfg);
    if (!server) {
        fprintf(stderr, "Failed to create server: %s\n", aegon_last_error());
        return 1;
    }

    if (aegon_server_listen(server, 8080, "127.0.0.1") != AEGON_OK) {
        fprintf(stderr, "Listen failed: %s\n", aegon_last_error());
        aegon_server_destroy(server);
        return 1;
    }

    printf("Upload server ready at http://127.0.0.1:8080/upload\n");
    printf("Try: curl -F 'description=test' -F 'file=@filename.png' http://127.0.0.1:8080/upload\n");

    aegon_server_destroy(server);
    return 0;
}
