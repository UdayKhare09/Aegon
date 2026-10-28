# 06 — Streaming & Async API: `async.h`, `websocket.h`, `sse.h`, `grpc.h`

- `async.h`, `websocket.h`, `sse.h` → **Phase 3**
- `grpc.h` → **Phase 4**

---

## 6.1 `include/aegon/c/async.h` — asynchronous FFI completion bridge

### Why it exists

Aegon workers are lock-free event loops. A C handler that must wait on something
external — a blocking driver, an FFI hop into Python/Go/Node, a disk job on a user
thread pool — must **not** block the worker. The bridge hands the request's
execution to the caller and resumes the C++ coroutine later, on the owning loop.

### Core dependency (Phase 3 prerequisite)

A new **C++** method is added to `core::EventLoop` (small, additive, benefits C++
users too — design mirrors `IoEngine::wakeup` which every backend already implements):

```cpp
/// Thread-safe: enqueue `fn` to run on this loop as soon as possible.
void EventLoop::post(std::function<void()> fn);
```

Mechanism: `post_completion`-style injection for io_uring (a lock-free MPSC queue
drained on wake), `eventfd` for epoll, `PostQueuedCompletionStatus` for IOCP.
`IoEngine::wakeup()` is the existing primitive that unblocks `submit_and_wait`.

### API

```c
#ifndef AEGON_C_ASYNC_H
#define AEGON_C_ASYNC_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef struct aegon_async aegon_async_t;   /* one-shot completion token */

/* Finalization callback. Runs ON THE OWNING WORKER THREAD after
 * aegon_async_complete() wakes the loop. `ctx` is valid again for this call only;
 * finalize the response here. After the callback returns, the token and the
 * request are recycled. */
typedef void (*aegon_async_done_fn)(aegon_ctx_t* ctx, void* userdata);

/* Detaches the current request from synchronous dispatch.
 * Must be called from INSIDE a handler registered via aegon_router_add_async
 * (below). On return, the route's C++ coroutine is suspended; the framework will
 * resume it when aegon_async_complete(token) is called from any thread.
 *
 * - One-shot: completing twice is a contract violation.
 * - The caller owns "when" — typical pattern: hand `token` to a user thread pool,
 *   do the blocking work there, then call aegon_async_complete.
 * - `done` may be NULL (response finalized before complete; rarely useful). */
AEGON_C_API aegon_async_t* aegon_async_create(aegon_ctx_t* ctx,
                                              aegon_async_done_fn done,
                                              void* userdata) noexcept;              /* P3 */

/* Signals completion. THREAD-SAFE from any thread. Wakes the owning event loop
 * (EventLoop::post); the `done` callback runs on the worker. */
AEGON_C_API void aegon_async_complete(aegon_async_t* token) noexcept;                /* P3 */

/* Async-capable route registration. Identical to aegon_router_add except the
 * handler may call aegon_async_create. If it doesn't, execution stays fully
 * synchronous (the wrapper falls through to the sync path). */
AEGON_C_API aegon_err_t aegon_router_add_async(aegon_router_t* router,
                                               aegon_method_t method,
                                               const char* pattern,
                                               aegon_handler_fn handler,
                                               void* userdata,
                                               aegon_cleanup_fn cleanup) noexcept;    /* P3 */
AEGON_C_API aegon_err_t aegon_group_add_async(aegon_group_t* group,
                                              aegon_method_t method,
                                              const char* pattern,
                                              aegon_handler_fn handler,
                                              void* userdata,
                                              aegon_cleanup_fn cleanup) noexcept;     /* P3 */

AEGON_C_END

#endif /* AEGON_C_ASYNC_H */
```

**C++ implementation sketch** (for `src/c/async.cpp` — documents the mechanics):

```cpp
// Handler wrapper: registers an AsyncHandler that:
//   1. stores a CompletionState{EventLoop*, std::coroutine_handle<>, done, userdata}
//      in a heap holder (aegon_async_t);
//   2. invokes the C handler with ctx;
//   3. co_awaits a ResumeAwaiter whose coroutine_handle is captured in the holder.
// aegon_async_complete: loop->post([holder]{ holder->done(ctx); holder->handle.resume(); });
```

---

## 6.2 `include/aegon/c/websocket.h` — WebSocket

C++ backing: `src/http/websocket/*` (`WebSocket`, `Message`, `Opcode`, `CloseCode`,
`WebSocketConfig` tuning already in `aegon_server_config_t`).

```c
#ifndef AEGON_C_WEBSOCKET_H
#define AEGON_C_WEBSOCKET_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

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
 * `payload` slices are valid only for the callback duration. */
typedef struct aegon_ws_callbacks {
    size_t struct_size;
    void (*on_open)   (aegon_ws_t* ws, aegon_str_t path, void* userdata);                 /* may be NULL */
    void (*on_message)(aegon_ws_t* ws, aegon_str_t payload, aegon_ws_opcode_t op,
                       void* userdata);                                                   /* may be NULL */
    void (*on_close)  (aegon_ws_t* ws, uint16_t code, aegon_str_t reason,
                       void* userdata);                                                   /* may be NULL */
    void (*on_error)  (aegon_ws_t* ws, aegon_str_t what, void* userdata);                 /* may be NULL */
} aegon_ws_callbacks_t;

AEGON_C_API void aegon_ws_callbacks_init(aegon_ws_callbacks_t* cbs) noexcept;        /* P3 */

/* Registers a WebSocket endpoint (Router::ws + 426 Upgrade Required fallback).
 * Send functions inside callbacks are SYNCHRONOUS from the C view: the wrapper
 * co_awaits the C++ send on the worker before dispatching the next frame. */
AEGON_C_API aegon_err_t aegon_router_ws(aegon_router_t* router,
                                        const char* pattern,
                                        const aegon_ws_callbacks_t* callbacks,
                                        void* userdata,
                                        aegon_cleanup_fn cleanup) noexcept;          /* P3 */
AEGON_C_API aegon_err_t aegon_group_ws(aegon_group_t* group,
                                       const char* pattern,
                                       const aegon_ws_callbacks_t* callbacks,
                                       void* userdata,
                                       aegon_cleanup_fn cleanup) noexcept;           /* P3 */

/* Zero-config echo endpoint (Router::ws_echo). */
AEGON_C_API aegon_err_t aegon_router_ws_echo(aegon_router_t* router,
                                             const char* pattern) noexcept;          /* P3 */

/* --- Senders (valid inside any session callback; thread-confined) --- */
AEGON_C_API aegon_err_t aegon_ws_send_text  (aegon_ws_t* ws, aegon_str_t payload) noexcept;    /* P3 */
AEGON_C_API aegon_err_t aegon_ws_send_binary(aegon_ws_t* ws, aegon_str_t payload) noexcept;    /* P3 */
AEGON_C_API aegon_err_t aegon_ws_send_ping  (aegon_ws_t* ws, aegon_str_t payload) noexcept;    /* P3 */
AEGON_C_API aegon_err_t aegon_ws_send_pong  (aegon_ws_t* ws, aegon_str_t payload) noexcept;    /* P3 */
AEGON_C_API aegon_err_t aegon_ws_close(aegon_ws_t* ws, uint16_t code,
                                       aegon_str_t reason) noexcept;                           /* P3 */

AEGON_C_API bool aegon_ws_is_open(aegon_ws_t* ws) noexcept;                            /* P3 */

AEGON_C_END

#endif /* AEGON_C_WEBSOCKET_H */
```

---

## 6.3 `include/aegon/c/sse.h` — Server-Sent Events

C++ backing: `src/http/SseStream.h` + `Context::sse()` + the connection layer's
`SseWriteFn`/`SseProvider`. Works identically on H1 (chunked), H2, H3.

```c
#ifndef AEGON_C_SSE_H
#define AEGON_C_SSE_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef struct aegon_sse aegon_sse_t;   /* connection-bound write channel (thread-safe) */

/* Called on the worker thread when the client disconnects (send failure / close).
 * The stream is dead; call aegon_sse_destroy from any thread afterwards. */
typedef void (*aegon_sse_on_close_fn)(void* userdata);

/* Upgrades the current response to text/event-stream (Context::sse).
 * Must be called inside a handler registered with aegon_router_add_async —
 * the wrapper suspends the coroutine and hands the stream to the caller.
 * Sets Content-Type, Cache-Control, X-Accel-Buffering headers.
 * Returns NULL if the route wasn't registered async (check aegon_last_error). */
AEGON_C_API aegon_sse_t* aegon_sse_create(aegon_ctx_t* ctx,
                                          aegon_sse_on_close_fn on_close,
                                          void* userdata) noexcept;                  /* P3 */

/* --- Senders. THREAD-SAFE from any thread (each send is posted onto the owning
 * event loop — see async.h mechanism). Payload is copied before return. --- */
AEGON_C_API aegon_err_t aegon_sse_send_event  (aegon_sse_t* sse, aegon_str_t name,
                                               aegon_str_t data) noexcept;           /* P3 */
AEGON_C_API aegon_err_t aegon_sse_send_data   (aegon_sse_t* sse, aegon_str_t data) noexcept; /* P3 */
AEGON_C_API aegon_err_t aegon_sse_send_comment(aegon_sse_t* sse, aegon_str_t text) noexcept; /* P3 */
AEGON_C_API aegon_err_t aegon_sse_send_retry  (aegon_sse_t* sse, uint32_t milliseconds) noexcept; /* P3 */

/* Marks the stream closed (subsequent sends are no-ops). THREAD-SAFE.
 * The connection layer sends the terminal EOF/zero-chunk. */
AEGON_C_API void aegon_sse_close(aegon_sse_t* sse) noexcept;                         /* P3 */

AEGON_C_API bool aegon_sse_is_open(aegon_sse_t* sse) noexcept;                       /* P3 */

/* Releases the handle (does NOT close the stream — close first if desired).
 * Must eventually be called exactly once. */
AEGON_C_API void aegon_sse_destroy(aegon_sse_t* sse) noexcept;                       /* P3 */

AEGON_C_END

#endif /* AEGON_C_SSE_H */
```

---

## 6.4 `include/aegon/c/grpc.h` — gRPC (raw-payload model)

C++ backing: `src/http/grpc/*` (`GrpcServiceGroup`, `GrpcContext`, `GrpcCodec`,
`GrpcStatus`, `GrpcInboundChannel`).

**Design constraint** (`01-architecture.md` §A10): typed protobuf codegen can't
cross C. The C ABI exposes gRPC's **wire payload**: the caller (de)serializes with
nanopb/protobuf-c/upb. Framing (5-byte prefix), trailers, and content-type are
handled by Aegon.

```c
#ifndef AEGON_C_GRPC_H
#define AEGON_C_GRPC_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* gRPC status codes (GrpcStatus.h). */
typedef enum aegon_grpc_status {
    AEGON_GRPC_OK                  = 0,
    AEGON_GRPC_CANCELLED           = 1,
    AEGON_GRPC_UNKNOWN             = 2,
    AEGON_GRPC_INVALID_ARGUMENT    = 3,
    AEGON_GRPC_DEADLINE_EXCEEDED   = 4,
    AEGON_GRPC_NOT_FOUND           = 5,
    AEGON_GRPC_ALREADY_EXISTS      = 6,
    AEGON_GRPC_PERMISSION_DENIED   = 7,
    AEGON_GRPC_RESOURCE_EXHAUSTED  = 8,
    AEGON_GRPC_FAILED_PRECONDITION = 9,
    AEGON_GRPC_ABORTED             = 10,
    AEGON_GRPC_OUT_OF_RANGE        = 11,
    AEGON_GRPC_UNIMPLEMENTED       = 12,
    AEGON_GRPC_INTERNAL            = 13,
    AEGON_GRPC_UNAVAILABLE         = 14,
    AEGON_GRPC_DATA_LOSS           = 15,
    AEGON_GRPC_UNAUTHENTICATED     = 16
} aegon_grpc_status_t;

typedef struct aegon_grpc_call aegon_grpc_call_t;   /* per-RPC context */

/* Unary handler. `request` is the raw message payload (5-byte frame STRIPPED —
 * pure protobuf bytes). Reply via aegon_grpc_reply or aegon_grpc_error exactly
 * once; failure to reply yields grpc-status 13 (C++ behavior). */
typedef void (*aegon_grpc_unary_fn)(aegon_grpc_call_t* call,
                                    aegon_str_t request,
                                    void* userdata);

/* Access the underlying HTTP context (headers, locals, app data). */
AEGON_C_API aegon_ctx_t* aegon_grpc_call_ctx(aegon_grpc_call_t* call) noexcept;      /* P4 */

/* Successful reply: `message` is raw protobuf payload (framing added by Aegon),
 * copied before return. Sets grpc-status: 0 trailer. */
AEGON_C_API aegon_err_t aegon_grpc_reply(aegon_grpc_call_t* call,
                                         aegon_str_t message) noexcept;              /* P4 */

/* Error reply (trailers-only). */
AEGON_C_API aegon_err_t aegon_grpc_error(aegon_grpc_call_t* call,
                                         aegon_grpc_status_t status,
                                         aegon_str_t message) noexcept;              /* P4 */

/* --- Registration (GrpcServiceGroup semantics: prefix + method name) ---
 * `service_prefix` e.g. "/my.package.MyService", `method_name` e.g. "GetSum". */
AEGON_C_API aegon_err_t aegon_router_grpc_unary(aegon_router_t* router,
                                                const char* service_prefix,
                                                const char* method_name,
                                                aegon_grpc_unary_fn handler,
                                                void* userdata,
                                                aegon_cleanup_fn cleanup) noexcept;  /* P4 */

/* --- Streaming RPCs (server / client / bidi) ---
 * Registered as async routes internally. Reading inbound messages and sending
 * outbound ones use the async.h bridge: callbacks below run on the worker; to
 * produce messages from elsewhere, capture the call via aegon_async_create
 * (streaming call ctx supports it) and drive sends from your own threads. */
typedef struct aegon_grpc_stream aegon_grpc_stream_t;

typedef struct aegon_grpc_stream_callbacks {
    size_t struct_size;
    /* Inbound message arrived (client/bidi streaming). */
    void (*on_message)(aegon_grpc_stream_t* stream, aegon_str_t payload, void* userdata);
    /* Client half-closed (END_STREAM). Send final reply/error after this. */
    void (*on_half_close)(aegon_grpc_stream_t* stream, void* userdata);
    /* Stream ended or errored; stream handle invalid after return. */
    void (*on_close)(aegon_grpc_stream_t* stream, void* userdata);
} aegon_grpc_stream_callbacks_t;

AEGON_C_API void aegon_grpc_stream_callbacks_init(aegon_grpc_stream_callbacks_t* cbs) noexcept; /* P4 */

AEGON_C_API aegon_err_t aegon_grpc_stream_send(aegon_grpc_stream_t* stream,
                                               aegon_str_t message) noexcept;        /* P4 */
AEGON_C_API aegon_err_t aegon_grpc_stream_error(aegon_grpc_stream_t* stream,
                                                aegon_grpc_status_t status,
                                                aegon_str_t message) noexcept;       /* P4 */

AEGON_C_API aegon_err_t aegon_router_grpc_server_streaming(aegon_router_t* router,
                                                           const char* service_prefix,
                                                           const char* method_name,
                                                           aegon_grpc_unary_fn on_request,
                                                           void* userdata,
                                                           aegon_cleanup_fn cleanup) noexcept; /* P4 */
AEGON_C_API aegon_err_t aegon_router_grpc_client_streaming(aegon_router_t* router,
                                                           const char* service_prefix,
                                                           const char* method_name,
                                                           const aegon_grpc_stream_callbacks_t* cbs,
                                                           void* userdata,
                                                           aegon_cleanup_fn cleanup) noexcept;   /* P4 */
AEGON_C_API aegon_err_t aegon_router_grpc_bidi_streaming(aegon_router_t* router,
                                                         const char* service_prefix,
                                                         const char* method_name,
                                                         const aegon_grpc_stream_callbacks_t* cbs,
                                                         void* userdata,
                                                         aegon_cleanup_fn cleanup) noexcept;     /* P4 */

AEGON_C_END

#endif /* AEGON_C_GRPC_H */
```

---

### Cross-check against C++ sources

| C API | C++ backing |
| :--- | :--- |
| `aegon_async_create/complete`, `*_add_async` | new `EventLoop::post` + `AsyncHandler` coroutine suspension/resume |
| `aegon_router_ws`, callbacks | `Router::ws`, `WebSocket::{on_message,on_text,on_binary,on_close,on_error}`, `Message` |
| `aegon_ws_send_*`, `aegon_ws_close` | `WebSocket::{send_text,send_binary,send_ping,send_pong,close}` |
| `aegon_router_ws_echo` | `Router::ws_echo` |
| `aegon_sse_*` | `Context::sse`, `SseStream::{event,data,comment,retry,close,is_open}`, `SseWriteFn` |
| `aegon_grpc_status_t` | `grpc::StatusCode` |
| `aegon_router_grpc_unary` | `GrpcServiceGroup::unary` (raw-codec specialization) |
| `aegon_grpc_reply/error` | `UnaryContext::{reply,error}` |
| streaming gRPC fns | `GrpcServiceGroup::{server_streaming,client_streaming,bidi_streaming}`, `GrpcInboundChannel` |
