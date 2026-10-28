#include <aegon/c/aegon.h>
#include <stdio.h>

static void hello(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_text(aegon_ctx_res(ctx), AEGON_STR("Hello from Aegon C ABI!\n"));
}

int main(void) {
    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);

    aegon_router_t* r = aegon_router_create();
    aegon_router_get(r, "/hello", hello, NULL, NULL);

    aegon_server_t* s = aegon_server_create(&r, &cfg);
    if (!s) {
        fprintf(stderr, "Failed to create server: %s\n", aegon_last_error());
        return 1;
    }

    aegon_err_t err = aegon_server_listen(s, 8080, "0.0.0.0");
    if (err != AEGON_OK) {
        fprintf(stderr, "Failed to listen on 8080: %s\n", aegon_last_error());
        aegon_server_destroy(s);
        return 1;
    }

    printf("Aegon C hello_world listening on http://0.0.0.0:8080/hello\n");
    err = aegon_server_run(s, 0);
    if (err != AEGON_OK) {
        fprintf(stderr, "Server run error: %s\n", aegon_last_error());
    }

    aegon_server_destroy(s);
    return err == AEGON_OK ? 0 : 1;
}

