#include <aegon/c/aegon.h>
#include <stdio.h>
#include <string.h>

static void get_user(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_res_t* res = aegon_ctx_res(ctx);

    aegon_str_t id = aegon_req_param(req, AEGON_STR("id"));
    aegon_str_t query = aegon_req_query_string(req);

    char buf[512];
    int len = snprintf(buf, sizeof(buf),
                       "{\"user_id\":\"%.*s\",\"query\":\"%.*s\"}\n",
                       (int)id.len, id.data ? id.data : "",
                       (int)query.len, query.data ? query.data : "");

    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_json(res, (aegon_str_t){ buf, (size_t)len });
}

static void create_user(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_res_t* res = aegon_ctx_res(ctx);

    aegon_str_t body = aegon_req_body(req);
    if (body.len == 0) {
        aegon_ctx_problem(ctx, AEGON_STATUS_BAD_REQUEST,
                          AEGON_STR("Invalid Body"),
                          AEGON_STR("Request body must not be empty"));
        return;
    }

    /* Echo the received JSON with status 201 */
    aegon_res_status(res, AEGON_STATUS_CREATED);
    aegon_res_json(res, body);
}

static void trigger_error(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_ctx_problem(ctx, AEGON_STATUS_UNPROCESSABLE_ENTITY,
                      AEGON_STR("Validation Error"),
                      AEGON_STR("Field 'email' is invalid"));
}

static void on_not_found(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_ctx_problem(ctx, AEGON_STATUS_NOT_FOUND,
                      AEGON_STR("Route Not Found"),
                      AEGON_STR("The requested resource does not exist"));
}

int main(void) {
    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);

    aegon_router_t* r = aegon_router_create();
    aegon_group_t* api = aegon_router_group(r, "/api");

    aegon_group_get(api, "/users/:id", get_user, NULL, NULL);
    aegon_group_post(api, "/users", create_user, NULL, NULL);
    aegon_group_get(api, "/validate", trigger_error, NULL, NULL);
    aegon_group_destroy(api);

    aegon_router_on_not_found(r, on_not_found, NULL, NULL);

    aegon_server_t* s = aegon_server_create(&r, &cfg);
    if (!s) {
        fprintf(stderr, "Failed to create server: %s\n", aegon_last_error());
        return 1;
    }

    aegon_server_listen(s, 8080, "0.0.0.0");
    printf("Aegon REST JSON service running on http://0.0.0.0:8080/api/users/:id\n");

    aegon_err_t err = aegon_server_run(s, 0);
    if (err != AEGON_OK) {
        fprintf(stderr, "Server error: %s\n", aegon_last_error());
    }

    aegon_server_destroy(s);
    return err == AEGON_OK ? 0 : 1;
}

