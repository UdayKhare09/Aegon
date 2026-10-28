#include <aegon/c/aegon.h>
#include <aegon/c/websocket.h>
#include <stdio.h>
#include <string.h>

static void ws_on_open(aegon_ws_t* ws, aegon_str_t path, void* ud) {
    (void)ud;
    printf("[ws] Client connected on path: %.*s\n", (int)path.len, path.data ? path.data : "");
    aegon_ws_send_text(ws, aegon_str("Welcome to Aegon C ABI WebSocket!"));
}

static void ws_on_message(aegon_ws_t* ws, aegon_str_t payload, aegon_ws_opcode_t op, void* ud) {
    (void)ud;
    printf("[ws] Received message (op=%d): %.*s\n", (int)op, (int)payload.len, payload.data ? payload.data : "");

    /* Echo message back with prefix */
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "Echo: %.*s", (int)payload.len, payload.data ? payload.data : "");
    if (n > 0) {
        aegon_ws_send_text(ws, aegon_str(buf));
    }
}

static void ws_on_close(aegon_ws_t* ws, uint16_t code, aegon_str_t reason, void* ud) {
    (void)ws;
    (void)ud;
    printf("[ws] Client disconnected (code=%u, reason=%.*s)\n", code, (int)reason.len, reason.data ? reason.data : "");
}

int main(void) {
    aegon_log_init(AEGON_LOG_INFO, false);
    printf("Starting Aegon C ABI WebSocket Echo example...\n");

    aegon_router_t* router = aegon_router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router: %s\n", aegon_last_error());
        return 1;
    }

    /* 1. Zero-config echo endpoint at /echo */
    aegon_router_ws_echo(router, "/echo");

    /* 2. Custom interactive endpoint at /ws */
    aegon_ws_callbacks_t cbs;
    aegon_ws_callbacks_init(&cbs);
    cbs.on_open = ws_on_open;
    cbs.on_message = ws_on_message;
    cbs.on_close = ws_on_close;

    aegon_err_t err = aegon_router_ws(router, "/ws", &cbs, NULL, NULL);
    if (err != AEGON_OK) {
        fprintf(stderr, "Failed to register ws route: %s\n", aegon_last_error());
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

    printf("WebSocket server listening on ws://127.0.0.1:8080/ws (and ws://127.0.0.1:8080/echo)\n");
    printf("Press Ctrl+C to stop.\n");

    aegon_server_run(server, 2);
    aegon_server_destroy(server);
    return 0;
}
