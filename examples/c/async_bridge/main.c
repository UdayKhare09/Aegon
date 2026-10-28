#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <aegon/c/aegon.h>
#include <aegon/c/async.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

struct compute_job {
    aegon_async_t* token;
    int input_val;
    long long result;
};

static void* compute_worker_thread(void* arg) {
    struct compute_job* job = (struct compute_job*)arg;

    /* Simulate intensive asynchronous computation off the Aegon worker thread */
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 50 * 1000000L; /* 50ms */
    nanosleep(&ts, NULL);

    job->result = (long long)job->input_val * job->input_val;

    /* Signal completion: thread-safe, safely posts callback back to owning Aegon event loop */
    aegon_async_complete(job->token);
    return NULL;
}

static void on_async_done(aegon_ctx_t* ctx, void* userdata) {
    struct compute_job* job = (struct compute_job*)userdata;

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"input\":%d,\"square\":%lld,\"status\":\"computed_async\"}",
             job->input_val, job->result);

    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_OK);
    aegon_res_json(res, aegon_str(buf));

    free(job);
}

static void handle_compute(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    struct compute_job* job = (struct compute_job*)malloc(sizeof(struct compute_job));
    if (!job) {
        aegon_res_status(aegon_ctx_res(ctx), AEGON_STATUS_INTERNAL_SERVER_ERROR);
        return;
    }

    job->input_val = 42;
    job->result = 0;

    /* Detach request from synchronous dispatch */
    job->token = aegon_async_create(ctx, on_async_done, job);
    if (!job->token) {
        free(job);
        aegon_res_status(aegon_ctx_res(ctx), AEGON_STATUS_INTERNAL_SERVER_ERROR);
        return;
    }

    pthread_t th;
    pthread_create(&th, NULL, compute_worker_thread, job);
    pthread_detach(th);
}

int main(void) {
    aegon_log_init(AEGON_LOG_INFO, false);
    printf("Starting Aegon C ABI Async Bridge example...\n");

    aegon_router_t* router = aegon_router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router: %s\n", aegon_last_error());
        return 1;
    }

    /* Register async route */
    aegon_err_t err = aegon_router_add_async(router, AEGON_METHOD_GET, "/compute", handle_compute, NULL, NULL);
    if (err != AEGON_OK) {
        fprintf(stderr, "Failed to register async route: %s\n", aegon_last_error());
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

    printf("Server listening on http://127.0.0.1:8080/compute\n");
    printf("Press Ctrl+C to stop.\n");

    /* For example verification, run single iteration or run server */
    aegon_server_run(server, 2);
    aegon_server_destroy(server);
    return 0;
}
