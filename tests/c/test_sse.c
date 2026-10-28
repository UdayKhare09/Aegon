#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <aegon/c/aegon.h>
#include <aegon/c/sse.h>
#include <aegon/c/async.h>
#include <aegon/c/server.h>
#include "check.h"
#include "fixture_server.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

static int g_sse_closed = 0;

static void sse_close_cb(void* ud) {
    (void)ud;
    g_sse_closed++;
}

struct sse_feed_arg {
    aegon_sse_t* sse;
};

static void* sse_feeder_thread(void* arg) {
    struct sse_feed_arg* a = (struct sse_feed_arg*)arg;
    aegon_sse_t* sse = a->sse;
    free(a);

    fixture_sleep_ms(30);

    /* Send comment */
    aegon_err_t err = aegon_sse_send_comment(sse, aegon_str("keepalive"));
    CHECK(err == AEGON_OK);

    /* Send retry */
    err = aegon_sse_send_retry(sse, 3000);
    CHECK(err == AEGON_OK);

    /* Send data */
    err = aegon_sse_send_data(sse, aegon_str("hello sse"));
    CHECK(err == AEGON_OK);

    /* Send named event */
    err = aegon_sse_send_event(sse, aegon_str("user_login"), aegon_str("alice"));
    CHECK(err == AEGON_OK);

    CHECK(aegon_sse_is_open(sse) == true);

    fixture_sleep_ms(30);

    /* Close stream from server */
    aegon_sse_close(sse);
    CHECK(aegon_sse_is_open(sse) == false);

    /* Subsequent send should fail */
    err = aegon_sse_send_data(sse, aegon_str("ignored"));
    CHECK(err == AEGON_ERR_STATE);

    /* Clean up handle */
    aegon_sse_destroy(sse);
    return NULL;
}

static void sse_handler(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_sse_t* sse = aegon_sse_create(ctx, sse_close_cb, NULL);
    CHECK(sse != NULL);

    struct sse_feed_arg* arg = (struct sse_feed_arg*)malloc(sizeof(struct sse_feed_arg));
    CHECK(arg != NULL);
    arg->sse = sse;

    pthread_t th;
    int rc = pthread_create(&th, NULL, sse_feeder_thread, arg);
    CHECK(rc == 0);
    pthread_detach(th);
}

static socket_t connect_socket(uint16_t port) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd != INVALID_SOCKET);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port = htons(port);

    int connected = 0;
    for (int retry = 0; retry < 50; ++retry) {
        if (connect(fd, (struct sockaddr*)&sin, sizeof(sin)) == 0) {
            connected = 1;
            break;
        }
        fixture_sleep_ms(10);
    }
    CHECK(connected == 1);
    return fd;
}

int main(void) {
    printf("[test_sse] Starting Server-Sent Events test suite...\n");

    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    aegon_err_t err = aegon_router_add_async(router, AEGON_METHOD_GET, "/events", sse_handler, NULL, NULL);
    CHECK(err == AEGON_OK);

    uint16_t port = get_ephemeral_port();
    aegon_server_t* server = aegon_server_create(&router, NULL);
    CHECK(server != NULL);
    CHECK(router == NULL);

    err = aegon_server_listen(server, port, "127.0.0.1");
    CHECK(err == AEGON_OK);

    err = aegon_server_run_async(server, 2);
    CHECK(err == AEGON_OK);
    fixture_sleep_ms(100);

    /* Client connects and receives SSE chunks */
    socket_t sock = connect_socket(port);
    const char* req = "GET /events HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    int sent = send(sock, req, (int)strlen(req), 0);
    CHECK(sent == (int)strlen(req));

    char buf[4096];
    size_t total_read = 0;
    while (total_read + 1 < sizeof(buf)) {
        int n = recv(sock, buf + total_read, (int)(sizeof(buf) - 1 - total_read), 0);
        if (n <= 0) break;
        total_read += (size_t)n;
    }
    buf[total_read] = '\0';
    CLOSE_SOCK(sock);

    CHECK(total_read > 0);
    CHECK(strstr(buf, "200 OK") != NULL);
    CHECK(strstr(buf, "Content-Type: text/event-stream") != NULL || strstr(buf, "content-type: text/event-stream") != NULL);
    CHECK(strstr(buf, ": keepalive\n\n") != NULL);
    CHECK(strstr(buf, "retry: 3000\n\n") != NULL);
    CHECK(strstr(buf, "data: hello sse\n\n") != NULL);
    CHECK(strstr(buf, "event: user_login\ndata: alice\n\n") != NULL);

    /* Graceful server shutdown */
    aegon_server_stop(server);
    aegon_server_join(server);
    aegon_server_destroy(server);

    printf("[test_sse] All tests PASSED!\n");
    return 0;
}
