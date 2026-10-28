#ifndef AEGON_C_WEBSOCKET_H
#define AEGON_C_WEBSOCKET_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

typedef struct aegon_ws aegon_ws_t;   /* ≡ aegon::http::websocket::WebSocket */

typedef enum aegon_ws_opcode {
    AEGON_WS_CONTINUATION = 0x0,
    AEGON_WS_TEXT         = 0x1,
    AEGON_WS_BINARY       = 0x2,
    AEGON_WS_CLOSE        = 0x8,
    AEGON_WS_PING         = 0x9,
    AEGON_WS_PONG         = 0xA
} aegon_ws_opcode_t;

typedef enum aegon_ws_close_code {
    AEGON_WS_CLOSE_NORMAL            = 1000,
    AEGON_WS_CLOSE_GOING_AWAY        = 1001,
    AEGON_WS_CLOSE_PROTOCOL_ERROR    = 1002,
    AEGON_WS_CLOSE_UNSUPPORTED_DATA  = 1003,
    AEGON_WS_CLOSE_NO_STATUS         = 1005,
    AEGON_WS_CLOSE_ABNORMAL          = 1006,
    AEGON_WS_CLOSE_INVALID_PAYLOAD   = 1007,
    AEGON_WS_CLOSE_POLICY_VIOLATION  = 1008,
    AEGON_WS_CLOSE_MESSAGE_TOO_BIG   = 1009,
    AEGON_WS_CLOSE_MANDATORY_EXT     = 1010,
    AEGON_WS_CLOSE_INTERNAL_ERROR    = 1011
} aegon_ws_close_code_t;

/* Session callbacks. All run on the owning worker thread.
 * payload slices are valid only for the callback duration. */
typedef struct aegon_ws_callbacks {
    size_t struct_size;
    void (*on_open)   (aegon_ws_t* ws, aegon_str_t path, void* userdata);                 /* may be NULL */
    void (*on_message)(aegon_ws_t* ws, aegon_str_t payload, aegon_ws_opcode_t op,
                       void* userdata);                                                   /* may be NULL */
    void (*on_close)  (aegon_ws_t* ws, uint16_t code, aegon_str_t reason,
                       void* userdata);                                                   /* may be NULL */
    void (*on_error)  (aegon_ws_t* ws, aegon_str_t what, void* userdata);                 /* may be NULL */
} aegon_ws_callbacks_t;

AEGON_C_API void aegon_ws_callbacks_init(aegon_ws_callbacks_t* cbs);

/* Registers a WebSocket endpoint (Router::ws + 426 Upgrade Required fallback).
 * Send functions inside callbacks are SYNCHRONOUS from the C view: the wrapper
 * co_awaits the C++ send on the worker before dispatching the next frame. */
AEGON_C_API aegon_err_t aegon_router_ws(aegon_router_t* router,
                                        const char* pattern,
                                        const aegon_ws_callbacks_t* callbacks,
                                        void* userdata,
                                        aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_group_ws(aegon_group_t* group,
                                       const char* pattern,
                                       const aegon_ws_callbacks_t* callbacks,
                                       void* userdata,
                                       aegon_cleanup_fn cleanup);

/* Zero-config echo endpoint (Router::ws_echo). */
AEGON_C_API aegon_err_t aegon_router_ws_echo(aegon_router_t* router,
                                             const char* pattern);

/* Senders (valid inside any session callback; thread-confined) */
AEGON_C_API aegon_err_t aegon_ws_send_text  (aegon_ws_t* ws, aegon_str_t payload);
AEGON_C_API aegon_err_t aegon_ws_send_binary(aegon_ws_t* ws, aegon_str_t payload);
AEGON_C_API aegon_err_t aegon_ws_send_ping  (aegon_ws_t* ws, aegon_str_t payload);
AEGON_C_API aegon_err_t aegon_ws_send_pong  (aegon_ws_t* ws, aegon_str_t payload);
AEGON_C_API aegon_err_t aegon_ws_close(aegon_ws_t* ws, uint16_t code,
                                       aegon_str_t reason);

AEGON_C_API bool aegon_ws_is_open(aegon_ws_t* ws);

AEGON_C_END

#endif /* AEGON_C_WEBSOCKET_H */

