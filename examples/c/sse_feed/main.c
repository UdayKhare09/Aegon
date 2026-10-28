#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <aegon/c/aegon.h>
#include <aegon/c/sse.h>
#include <aegon/c/async.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

static void sse_on_disconnect(void* ud) {
    (void)ud;
    printf("[sse] Client disconnected\n");
}

static void* ticker_thread_fn(void* arg) {
    aegon_sse_t* sse = (aegon_sse_t*)arg;

    /* Advise client to reconnect after 2000ms if connection drops */
    aegon_sse_send_retry(sse, 2000);

    /* Stream 5 ticker events then close */
    for (int i = 1; i <= 5 && aegon_sse_is_open(sse); ++i) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 500 * 1000000L; /* 500ms */
        nanosleep(&ts, NULL);

        char buf[128];
        snprintf(buf, sizeof(buf), "{\"tick\":%d,\"price\":%.2f}", i, 100.0 + (double)i * 1.5);
        aegon_sse_send_event(sse, aegon_str("ticker"), aegon_str(buf));
        aegon_sse_send_comment(sse, aegon_str("heartbeat"));
    }

    if (aegon_sse_is_open(sse)) {
        aegon_sse_close(sse);
    }
    aegon_sse_destroy(sse);
    return NULL;
}

static void handle_sse_feed(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_sse_t* sse = aegon_sse_create(ctx, sse_on_disconnect, NULL);
    if (!sse) {
        aegon_res_status(aegon_ctx_res(ctx), AEGON_STATUS_INTERNAL_SERVER_ERROR);
        return;
    }

    pthread_t th;
    pthread_create(&th, NULL, ticker_thread_fn, sse);
    pthread_detach(th);
}

int main(void) {
    aegon_log_init(AEGON_LOG_INFO, false);
    printf("Starting Aegon C ABI Server-Sent Events example...\n");

    aegon_router_t* router = aegon_router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router: %s\n", aegon_last_error());
        return 1;
    }

    aegon_err_t err = aegon_router_add_async(router, AEGON_METHOD_GET, "/feed", handle_sse_feed, NULL, NULL);
    if (err != AEGON_OK) {
        fprintf(stderr, "Failed to register sse route: %s\n", aegon_last_error());
        return 1;
    }

    aegon_server_t* server = aegon_server_create(&router, NULL);
    if (!server) {
        fprintf(stderr, "Failed to create server: %s\n", aegon_last_error());
        return 1;
    }

    err = aegon_server_listen(server, 8080, "0.0.0.0");
    if (err != AEGON_OK) {
        fprintf(stderr, "Failed to listen: %s\n", aegon_last_error());
        return 1;
    }

    printf("SSE server listening on http://127.0.0.1:8080/feed\n");
    printf("Test with: curl -N http://127.0.0.1:8080/feed\n");
    printf("Press Ctrl+C to stop.\n");

    aegon_server_run(server, 2);
    aegon_server_destroy(server);
    return 0;
}
