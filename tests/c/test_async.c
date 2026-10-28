#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <aegon/c/aegon.h>
#include <aegon/c/async.h>
#include <aegon/c/server.h>
#include "check.h"
#include "fixture_server.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_started = 0;
static int g_stopped = 0;
static int g_worker_ran = 0;

static void on_start_hook(aegon_server_t* s, void* ud) {
    (void)s;
    int* p = (int*)ud;
    if (p) (*p)++;
}

static void on_stop_hook(aegon_server_t* s, void* ud) {
    (void)s;
    int* p = (int*)ud;
    if (p) (*p)++;
}

static void worker_hook(void* ud) {
    int* p = (int*)ud;
    if (p) (*p)++;
}

/* Synchronous fallback handler in async route */
static void sync_handler(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, 200);
    aegon_res_text(res, aegon_str("sync fallback"));
}

/* Thread pool simulation for async handler */
struct async_worker_arg {
    aegon_async_t* token;
};

static void* async_thread_fn(void* arg) {
    struct async_worker_arg* a = (struct async_worker_arg*)arg;
    fixture_sleep_ms(20);
    aegon_async_complete(a->token);
    /* Test double complete guard: calling complete again must be a safe no-op */
    aegon_async_complete(a->token);
    free(a);
    return NULL;
}

static void async_done_cb(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, 200);
    aegon_res_text(res, aegon_str("async resolved"));
}

static void async_handler(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_async_t* token = aegon_async_create(ctx, async_done_cb, NULL);
    CHECK(token != NULL);

    struct async_worker_arg* arg = (struct async_worker_arg*)malloc(sizeof(struct async_worker_arg));
    CHECK(arg != NULL);
    arg->token = token;

    pthread_t th;
    int rc = pthread_create(&th, NULL, async_thread_fn, arg);
    CHECK(rc == 0);
    pthread_detach(th);
}

static void non_async_handler(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    /* Calling aegon_async_create on a route not registered via add_async must return NULL */
    aegon_async_t* token = aegon_async_create(ctx, NULL, NULL);
    CHECK(token == NULL);
    const char* err = aegon_last_error();
    CHECK(err != NULL && strlen(err) > 0);

    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, 200);
    aegon_res_text(res, aegon_str("handled"));
}

static void group_async_handler(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_async_t* token = aegon_async_create(ctx, async_done_cb, NULL);
    CHECK(token != NULL);

    struct async_worker_arg* arg = (struct async_worker_arg*)malloc(sizeof(struct async_worker_arg));
    CHECK(arg != NULL);
    arg->token = token;

    pthread_t th;
    int rc = pthread_create(&th, NULL, async_thread_fn, arg);
    CHECK(rc == 0);
    pthread_detach(th);
}

int main(void) {
    printf("[test_async] Starting Async Bridge test suite...\n");

    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    /* 1. Register async route that stays synchronous */
    aegon_err_t err = aegon_router_add_async(router, AEGON_METHOD_GET, "/sync", sync_handler, NULL, NULL);
    CHECK(err == AEGON_OK);

    /* 2. Register async route that detaches to background thread */
    err = aegon_router_add_async(router, AEGON_METHOD_GET, "/async", async_handler, NULL, NULL);
    CHECK(err == AEGON_OK);

    /* 3. Register regular route to test misuse guard */
    err = aegon_router_add(router, AEGON_METHOD_GET, "/non_async", non_async_handler, NULL, NULL);
    CHECK(err == AEGON_OK);

    /* 4. Register group async route */
    aegon_group_t* grp = aegon_router_group(router, "/api");
    CHECK(grp != NULL);
    err = aegon_group_add_async(grp, AEGON_METHOD_GET, "/work", group_async_handler, NULL, NULL);
    CHECK(err == AEGON_OK);
    aegon_group_destroy(grp);

    uint16_t port = get_ephemeral_port();
    aegon_server_t* server = aegon_server_create(&router, NULL);
    CHECK(server != NULL);
    CHECK(router == NULL);

    /* Register lifecycle hooks */
    err = aegon_server_on_start(server, on_start_hook, &g_started, NULL);
    CHECK(err == AEGON_OK);
    err = aegon_server_on_stop(server, on_stop_hook, &g_stopped, NULL);
    CHECK(err == AEGON_OK);
    err = aegon_server_spawn_worker(server, worker_hook, &g_worker_ran, NULL);
    CHECK(err == AEGON_OK);

    err = aegon_server_listen(server, port, "127.0.0.1");
    CHECK(err == AEGON_OK);

    err = aegon_server_run_async(server, 2);
    CHECK(err == AEGON_OK);
    fixture_sleep_ms(100);

    /* Verify startup hook ran */
    CHECK(g_started >= 1);
    /* Verify background worker ran */
    CHECK(g_worker_ran >= 1);

    char resp[4096];

    /* Test 1: Synchronous fallback inside async route */
    int n = http_exchange(port, "GET /sync HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "sync fallback") != NULL);

    /* Test 2: Asynchronous completion from foreign thread */
    n = http_exchange(port, "GET /async HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "async resolved") != NULL);

    /* Test 3: Calling aegon_async_create on regular route */
    n = http_exchange(port, "GET /non_async HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "handled") != NULL);

    /* Test 4: Group async route */
    n = http_exchange(port, "GET /api/work HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "async resolved") != NULL);

    /* Graceful stop */
    aegon_server_stop(server);
    aegon_server_join(server);

    /* Verify stop hook ran */
    CHECK(g_stopped >= 1);

    aegon_server_destroy(server);

    printf("[test_async] All tests PASSED!\n");
    return 0;
}
