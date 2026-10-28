#include "aegon/c/server.h"
#include <aegon/c/aegon.h>
#include <stdio.h>

static void handle_secure(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_res_t* res = aegon_ctx_res(ctx);

    const char* ver_str = aegon_http_version_cstr(aegon_req_version(req));

    char msg[256];
    int len = snprintf(msg, sizeof(msg),
                       "Secure TLS connection established via %s (ALPN)!\n",
                       ver_str);

    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, (aegon_str_t){ msg, (size_t)len });
}

int main(int argc, char** argv) {
    const char* cert_file = argc > 1 ? argv[1] : "";
    const char* key_file = argc > 2 ? argv[2] : "";

    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);

    aegon_router_t* r = aegon_router_create();
    aegon_router_get(r, "/secure", handle_secure, NULL, NULL);

    aegon_server_t* s = aegon_server_create(&r, &cfg);
    if (!s) {
        fprintf(stderr, "Failed to create server: %s\n", aegon_last_error());
        return 1;
    }

    /* Configure TLS: empty strings use Aegon embedded dev certificate */
    aegon_err_t err = aegon_server_enable_tls(s, cert_file, key_file);
    if (err != AEGON_OK) {
        fprintf(stderr, "Failed to enable TLS: %s\n", aegon_last_error());
        aegon_server_destroy(s);
        return 1;
    }

    aegon_server_listen(s, 8080, "0.0.0.0");
    err = aegon_server_listen_tls(s, 8443, "0.0.0.0");
    if (err != AEGON_OK) {
        fprintf(stderr, "Failed to listen on TLS port 8443: %s\n", aegon_last_error());
        aegon_server_destroy(s);
        return 1;
    }

    printf("Aegon HTTPS/H2 server listening on https://0.0.0.0:8443/secure\n");
    err = aegon_server_run(s, 0);
    if (err != AEGON_OK) {
        fprintf(stderr, "Server error: %s\n", aegon_last_error());
    }

    aegon_server_destroy(s);
    return err == AEGON_OK ? 0 : 1;
}

