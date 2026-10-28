#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <aegon/c/aegon.h>
#include <aegon/c/websocket.h>
#include <aegon/c/server.h>
#include "check.h"
#include "fixture_server.h"

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
#include <endian.h>
#endif

static int g_open_called = 0;
static int g_message_called = 0;
static int g_close_called = 0;

static void ws_on_open(aegon_ws_t* ws, aegon_str_t path, void* ud) {
    (void)ud;
    g_open_called++;
    CHECK(aegon_ws_is_open(ws));
    CHECK(path.len > 0);
}

static void ws_on_message(aegon_ws_t* ws, aegon_str_t payload, aegon_ws_opcode_t op, void* ud) {
    (void)ud;
    g_message_called++;
    CHECK(aegon_ws_is_open(ws));
    CHECK(op == AEGON_WS_TEXT);

    if (payload.len == 9 && memcmp(payload.data, "test-ping", 9) == 0) {
        aegon_err_t err = aegon_ws_send_text(ws, aegon_str("test-pong"));
        CHECK(err == AEGON_OK);
    }
}

static void ws_on_close(aegon_ws_t* ws, uint16_t code, aegon_str_t reason, void* ud) {
    (void)ws;
    (void)code;
    (void)reason;
    (void)ud;
    g_close_called++;
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

static void send_masked_text_frame(socket_t sock, const char* text) {
    size_t len = strlen(text);
    CHECK(len < 126);

    uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    uint8_t buf[256];
    buf[0] = 0x81; /* FIN + Text */
    buf[1] = (uint8_t)(0x80 | len); /* Masked */
    memcpy(buf + 2, mask, 4);

    for (size_t i = 0; i < len; ++i) {
        buf[6 + i] = (uint8_t)(text[i] ^ mask[i % 4]);
    }

    int sent = send(sock, (const char*)buf, (int)(6 + len), 0);
    CHECK(sent == (int)(6 + len));
}

static void send_masked_close_frame(socket_t sock, uint16_t code) {
    uint8_t mask[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t buf[8];
    buf[0] = 0x88; /* FIN + Close */
    buf[1] = 0x82; /* Masked + 2 bytes */
    memcpy(buf + 2, mask, 4);

    uint16_t be = htons(code);
    uint8_t* p = (uint8_t*)&be;
    buf[6] = (uint8_t)(p[0] ^ mask[0]);
    buf[7] = (uint8_t)(p[1] ^ mask[1]);

    int sent = send(sock, (const char*)buf, 8, 0);
    CHECK(sent == 8);
}

int main(void) {
    printf("[test_websocket] Starting WebSocket test suite...\n");

    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    /* 1. Zero-config echo endpoint */
    aegon_err_t err = aegon_router_ws_echo(router, "/echo");
    CHECK(err == AEGON_OK);

    /* 2. Custom callback endpoint */
    aegon_ws_callbacks_t cbs;
    aegon_ws_callbacks_init(&cbs);
    cbs.on_open = ws_on_open;
    cbs.on_message = ws_on_message;
    cbs.on_close = ws_on_close;

    err = aegon_router_ws(router, "/custom", &cbs, NULL, NULL);
    CHECK(err == AEGON_OK);

    /* 3. Group endpoint */
    aegon_group_t* grp = aegon_router_group(router, "/v1");
    CHECK(grp != NULL);
    err = aegon_group_ws(grp, "/chat", &cbs, NULL, NULL);
    CHECK(err == AEGON_OK);
    aegon_group_destroy(grp);

    uint16_t port = get_ephemeral_port();
    aegon_server_t* server = aegon_server_create(&router, NULL);
    CHECK(server != NULL);
    CHECK(router == NULL);

    err = aegon_server_listen(server, port, "127.0.0.1");
    CHECK(err == AEGON_OK);

    err = aegon_server_run_async(server, 2);
    CHECK(err == AEGON_OK);
    fixture_sleep_ms(100);

    /* Test 1: Plain HTTP GET without upgrade headers -> 426 Upgrade Required */
    char resp[4096];
    int n = http_exchange(port, "GET /echo HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "426 Upgrade Required") != NULL);

    /* Test 2: WebSocket handshake on /echo */
    socket_t sock = connect_socket(port);
    const char* upgrade_req =
        "GET /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    int sent = send(sock, upgrade_req, (int)strlen(upgrade_req), 0);
    CHECK(sent == (int)strlen(upgrade_req));

    int recvd = recv(sock, resp, sizeof(resp) - 1, 0);
    CHECK(recvd > 0);
    resp[recvd] = '\0';
    CHECK(strstr(resp, "101 Switching Protocols") != NULL);
    CHECK(strstr(resp, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != NULL);

    /* Send masked text frame "Hello Aegon Echo!" */
    send_masked_text_frame(sock, "Hello Aegon Echo!");

    /* Receive unmasked text frame from server */
    recvd = recv(sock, resp, sizeof(resp), 0);
    CHECK(recvd > 2);
    CHECK((uint8_t)resp[0] == 0x81); /* FIN + Text */
    size_t payload_len = (uint8_t)resp[1] & 0x7F;
    CHECK(payload_len == strlen("Hello Aegon Echo!"));
    CHECK(memcmp(resp + 2, "Hello Aegon Echo!", payload_len) == 0);

    /* Close handshake */
    send_masked_close_frame(sock, 1000);
    recvd = recv(sock, resp, sizeof(resp), 0);
    CHECK(recvd >= 2);
    CHECK((uint8_t)resp[0] == 0x88); /* Server close frame */
    CLOSE_SOCK(sock);

    /* Test 3: Custom callback endpoint /custom */
    socket_t sock2 = connect_socket(port);
    const char* custom_req =
        "GET /custom HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    sent = send(sock2, custom_req, (int)strlen(custom_req), 0);
    CHECK(sent == (int)strlen(custom_req));

    recvd = recv(sock2, resp, sizeof(resp) - 1, 0);
    CHECK(recvd > 0);
    resp[recvd] = '\0';
    CHECK(strstr(resp, "101 Switching Protocols") != NULL);

    /* Send "test-ping" */
    send_masked_text_frame(sock2, "test-ping");

    /* Expect "test-pong" reply */
    recvd = recv(sock2, resp, sizeof(resp), 0);
    CHECK(recvd > 2);
    CHECK((uint8_t)resp[0] == 0x81);
    payload_len = (uint8_t)resp[1] & 0x7F;
    CHECK(payload_len == strlen("test-pong"));
    CHECK(memcmp(resp + 2, "test-pong", payload_len) == 0);
    CHECK(g_open_called >= 1);
    CHECK(g_message_called >= 1);

    send_masked_close_frame(sock2, 1000);
    recvd = recv(sock2, resp, sizeof(resp), 0);
    CLOSE_SOCK(sock2);

    fixture_sleep_ms(30);
    CHECK(g_close_called >= 1);

    /* Clean shutdown */
    aegon_server_stop(server);
    aegon_server_join(server);
    aegon_server_destroy(server);

    printf("[test_websocket] All tests PASSED!\n");
    return 0;
}
