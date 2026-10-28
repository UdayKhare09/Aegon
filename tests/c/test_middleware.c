#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"

static char g_exec_trace[256];
static int g_cleanup_count = 0;

static void trace_append(const char* step) {
    if (g_exec_trace[0] != '\0') {
        strcat(g_exec_trace, " -> ");
    }
    strcat(g_exec_trace, step);
}

static void mw_cleanup(void* ud) {
    (void)ud;
    g_cleanup_count++;
}

/* Global MW 1: Pre/Post order */
static void mw_global_1(aegon_ctx_t* ctx, aegon_next_t* next, void* ud) {
    (void)ud;
    trace_append("M1_PRE");
    aegon_next_call(next, ctx);
    trace_append("M1_POST");
}

/* Global MW 2: Pre/Post order */
static void mw_global_2(aegon_ctx_t* ctx, aegon_next_t* next, void* ud) {
    (void)ud;
    trace_append("M2_PRE");
    aegon_next_call(next, ctx);
    trace_append("M2_POST");
}

/* Short-circuiting middleware */
static void mw_short_circuit(aegon_ctx_t* ctx, aegon_next_t* next, void* ud) {
    (void)ud;
    aegon_req_t* req = aegon_ctx_req(ctx);
    aegon_str_t q = aegon_req_query_string(req);
    if (q.len > 0 && strstr(q.data, "block=1") != NULL) {
        trace_append("BLOCKED");
        aegon_res_t* res = aegon_ctx_res(ctx);
        aegon_res_status(res, AEGON_STATUS_FORBIDDEN);
        aegon_res_text(res, AEGON_STR("access denied"));
        return; /* Do not call aegon_next_call */
    }
    aegon_next_call(next, ctx);
}

/* Group middleware */
static void mw_group(aegon_ctx_t* ctx, aegon_next_t* next, void* ud) {
    (void)ud;
    trace_append("GRP_MW");
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_header(res, AEGON_STR("X-Group-Applied"), AEGON_STR("true"));
    aegon_next_call(next, ctx);
}

/* Route-specific middleware */
static void mw_route_specific(aegon_ctx_t* ctx, aegon_next_t* next, void* ud) {
    (void)ud;
    trace_append("ROUTE_MW");
    aegon_next_call(next, ctx);
}

/* Handler */
static void handle_onion(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    trace_append("HANDLER");
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("onion ok"));
}

/* Group Handler */
static void handle_group_route(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    trace_append("GRP_HANDLER");
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_text(res, AEGON_STR("group ok"));
}

int main(void) {
    /* 1. Argument validation */
    CHECK_INT_EQ(aegon_router_use(NULL, mw_global_1, NULL, NULL), AEGON_ERR_INVALID_ARG);
    CHECK_INT_EQ(aegon_group_use(NULL, mw_group, NULL, NULL), AEGON_ERR_INVALID_ARG);
    aegon_next_call(NULL, NULL); /* Safe no-op */

    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    /* Register middlewares with cleanup */
    CHECK_INT_EQ(aegon_router_use(router, mw_global_1, (void*)1, mw_cleanup), AEGON_OK);
    CHECK_INT_EQ(aegon_router_use(router, mw_global_2, (void*)2, mw_cleanup), AEGON_OK);
    CHECK_INT_EQ(aegon_router_use(router, mw_short_circuit, (void*)3, mw_cleanup), AEGON_OK);

    /* Standard route */
    CHECK_INT_EQ(aegon_router_get(router, "/onion", handle_onion, NULL, NULL), AEGON_OK);

    /* Route with per-route middleware */
    aegon_middleware_slot_t route_mws[1] = {
        { .fn = mw_route_specific, .userdata = (void*)4, .cleanup = mw_cleanup }
    };
    CHECK_INT_EQ(aegon_router_add_mw(router, AEGON_METHOD_GET, "/per_route",
                                     route_mws, 1, handle_onion, NULL, NULL), AEGON_OK);

    /* Group with group middleware */
    aegon_group_t* grp = aegon_router_group(router, "/api");
    CHECK(grp != NULL);
    CHECK_INT_EQ(aegon_group_use(grp, mw_group, (void*)5, mw_cleanup), AEGON_OK);
    CHECK_INT_EQ(aegon_group_get(grp, "/data", handle_group_route, NULL, NULL), AEGON_OK);

    aegon_middleware_slot_t grp_route_mws[1] = {
        { .fn = mw_route_specific, .userdata = (void*)6, .cleanup = mw_cleanup }
    };
    CHECK_INT_EQ(aegon_group_add_mw(grp, AEGON_METHOD_GET, "/route_mw",
                                    grp_route_mws, 1, handle_group_route, NULL, NULL), AEGON_OK);
    aegon_group_destroy(grp);

    /* Create and start server */
    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);
    aegon_server_t* server = aegon_server_create(&router, &cfg);
    CHECK(server != NULL);

    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 2), AEGON_OK);
    fixture_sleep_ms(50);

    char resp[1024];

    /* Test 1: Full onion traversal */
    g_exec_trace[0] = '\0';
    CHECK(http_exchange(port, "GET /onion HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "onion ok") != NULL);
    CHECK_STR_EQ(g_exec_trace, "M1_PRE -> M2_PRE -> HANDLER -> M2_POST -> M1_POST");

    /* Test 2: Short-circuiting */
    g_exec_trace[0] = '\0';
    CHECK(http_exchange(port, "GET /onion?block=1 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "access denied") != NULL);
    /* M1_PRE -> M2_PRE -> BLOCKED -> M2_POST -> M1_POST (HANDLER not called!) */
    CHECK_STR_EQ(g_exec_trace, "M1_PRE -> M2_PRE -> BLOCKED -> M2_POST -> M1_POST");

    /* Test 3: Per-route middleware */
    g_exec_trace[0] = '\0';
    CHECK(http_exchange(port, "GET /per_route HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK_STR_EQ(g_exec_trace, "M1_PRE -> M2_PRE -> ROUTE_MW -> HANDLER -> M2_POST -> M1_POST");

    /* Test 4: Group middleware */
    g_exec_trace[0] = '\0';
    CHECK(http_exchange(port, "GET /api/data HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "X-Group-Applied: true") != NULL);
    CHECK_STR_EQ(g_exec_trace, "M1_PRE -> M2_PRE -> GRP_MW -> GRP_HANDLER -> M2_POST -> M1_POST");

    /* Test 5: Group + per-route middleware */
    g_exec_trace[0] = '\0';
    CHECK(http_exchange(port, "GET /api/route_mw HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "X-Group-Applied: true") != NULL);
    CHECK_STR_EQ(g_exec_trace, "M1_PRE -> M2_PRE -> GRP_MW -> ROUTE_MW -> GRP_HANDLER -> M2_POST -> M1_POST");

    /* Stop server */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    /* Verify cleanups ran */
    CHECK_INT_EQ(g_cleanup_count, 6);

    return 0;
}
