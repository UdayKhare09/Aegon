#include <aegon/c/aegon.h>
#include <stdio.h>

/* Custom timing & header-injection onion middleware */
static void mw_timing_tracer(aegon_ctx_t* ctx, aegon_next_t* next, void* ud) {
    (void)ud;
    aegon_log_write(AEGON_LOG_INFO, AEGON_STR("-> Inbound request entered custom onion middleware"));

    /* Invoke downstream chain (next middleware or route handler) */
    aegon_next_call(next, ctx);

    /* Post-processing */
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_header(res, AEGON_STR("X-Aegon-Middleware"), AEGON_STR("active"));
    aegon_log_write(AEGON_LOG_INFO, AEGON_STR("<- Outbound response leaving custom onion middleware"));
}

static void handle_api_status(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_json(res, AEGON_STR("{\"status\":\"ok\",\"service\":\"middleware-demo\"}"));
}

int main(void) {
    aegon_log_init(AEGON_LOG_INFO, false);
    printf("Starting Aegon C ABI Middleware Chain example...\n");

    aegon_router_t* router = aegon_router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router: %s\n", aegon_last_error());
        return 1;
    }

    /* 1. Built-in Security Headers */
    aegon_security_config_t sec_cfg;
    aegon_security_config_init(&sec_cfg);
    sec_cfg.content_security_policy = "default-src 'self'";
    aegon_router_use_security_headers(router, &sec_cfg);

    /* 2. Built-in CORS (Permissive) */
    aegon_cors_config_t cors_cfg;
    aegon_cors_config_init(&cors_cfg);
    const char* origins[] = { "*" };
    cors_cfg.allowed_origins = origins;
    cors_cfg.allowed_origins_count = 1;
    aegon_router_use_cors(router, &cors_cfg);

    /* 3. Built-in Compression */
    aegon_compress_config_t comp_cfg;
    aegon_compress_config_init(&comp_cfg);
    aegon_router_use_compress(router, &comp_cfg);

    /* 4. Custom Onion Middleware */
    aegon_router_use(router, mw_timing_tracer, NULL, NULL);

    /* Routes */
    aegon_router_get(router, "/status", handle_api_status, NULL, NULL);

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

    printf("Server listening at http://127.0.0.1:8080/status\n");
    printf("Run: curl -v -H 'Origin: http://example.com' http://127.0.0.1:8080/status\n");

    /* For demonstration we initialize and stop cleanly */
    aegon_server_destroy(server);
    return 0;
}
