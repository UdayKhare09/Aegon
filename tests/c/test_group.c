#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static int g_grp_cleanup_count = 0;
static void group_cleanup_cb(void* ud) {
    int* p = (int*)ud;
    if (p) (*p)++;
}

static void handle_v1_users(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_text(aegon_ctx_res(ctx), AEGON_STR("v1 users"));
}

static void handle_v1_admin_metrics(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_text(aegon_ctx_res(ctx), AEGON_STR("v1 admin metrics"));
}

int main(void) {
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    /* 1. Create Route Group /api/v1 */
    aegon_group_t* v1 = aegon_router_group(router, "/api/v1");
    CHECK(v1 != NULL);
    CHECK_INT_EQ(aegon_group_get(v1, "/users", handle_v1_users, &g_grp_cleanup_count, group_cleanup_cb), AEGON_OK);

    /* 2. Create Nested Group /admin under /api/v1 */
    aegon_group_t* admin = aegon_group_group(v1, "/admin");
    CHECK(admin != NULL);
    CHECK_INT_EQ(aegon_group_get(admin, "/metrics", handle_v1_admin_metrics, NULL, NULL), AEGON_OK);

    /* Groups can be destroyed after routes are added to the router */
    aegon_group_destroy(admin);
    aegon_group_destroy(v1);

    /* 3. Build & run server */
    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);

    aegon_server_t* server = aegon_server_create(&router, &cfg);
    CHECK(server != NULL);
    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 1), AEGON_OK);

    fixture_sleep_ms(50);
    CHECK(aegon_server_is_running(server));

    char resp[2048];

    /* Test /api/v1/users */
    CHECK(http_exchange(port, "GET /api/v1/users HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "v1 users") != NULL);

    /* Test /api/v1/admin/metrics */
    CHECK(http_exchange(port, "GET /api/v1/admin/metrics HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "v1 admin metrics") != NULL);

    /* 4. Teardown server and verify cleanup callback is invoked when server (owning the router) is destroyed */
    CHECK_INT_EQ(g_grp_cleanup_count, 0);

    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    CHECK_INT_EQ(g_grp_cleanup_count, 1);

    return 0;
}

