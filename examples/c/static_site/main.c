#include <aegon/c/aegon.h>
#include <stdio.h>
#include <string.h>

static size_t custom_cache_policy(aegon_str_t path, bool is_fallback,
                                   char* buf, size_t buf_cap, void* ud) {
    (void)path;
    (void)ud;
    const char* val = is_fallback
        ? "no-cache, no-store, must-revalidate"
        : "public, max-age=86400, immutable";
    size_t len = strlen(val);
    if (len + 1 > buf_cap) return 0;
    memcpy(buf, val, len + 1);
    return len;
}

int main(void) {
    aegon_log_init(AEGON_LOG_INFO, false);
    printf("Starting Aegon C ABI Static Site server example...\n");

    aegon_router_t* router = aegon_router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router: %s\n", aegon_last_error());
        return 1;
    }

    aegon_static_files_config_t cfg;
    aegon_static_files_config_init(&cfg);
    cfg.spa = true;                       /* Single Page Application HTML5 history fallback */
    cfg.cache_control = custom_cache_policy;

    /* Serve current directory or web public folder */
    if (aegon_router_static_files(router, "/", ".", &cfg) != AEGON_OK) {
        fprintf(stderr, "Failed to configure static files: %s\n", aegon_last_error());
        aegon_router_destroy(router);
        return 1;
    }

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

    printf("Static site server ready at http://127.0.0.1:8080/\n");
    aegon_server_destroy(server);
    return 0;
}
