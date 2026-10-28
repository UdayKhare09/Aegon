# Aegon C ABI (`libaegon_c`) — Specification & Roadmap

> Status: **Planning** · Target start: Phase 0 · Aegon version at time of writing: 0.2.1

This directory is the authoritative specification for exposing Aegon's HTTP engine
(`aegon_http` and its dependencies `aegon_core`, `aegon_log`) to **pure C consumers** and
**foreign-function interfaces** (Rust, Go, Python, Zig, …) through a stable C ABI.

Read in order:

| File | Contents |
| :--- | :--- |
| `README.md` (this file) | Vision, scope, design principles, layout, phase overview |
| `01-architecture.md` | Binding architectural decisions (strings, ownership, errors, threading, ABI policy) |
| `02-api-core.md` | `export.h`, `aegon.h`, `types.h`, `error.h`, `log.h` — full API listing |
| `03-api-http.md` | `request.h`, `response.h`, `context.h`, `multipart.h` — full API listing |
| `04-api-server.md` | `router.h`, `server.h` (+ all config structs) — full API listing |
| `05-api-middleware.md` | Custom middleware + CORS / Compress / SecurityHeaders / AccessLog / Auth / StaticFiles |
| `06-api-streaming.md` | `async.h` (FFI bridge), `websocket.h`, `sse.h`, `grpc.h` |
| `07-build-testing.md` | CMake, packaging, symbol visibility, pkg-config, test suite, examples, CI |
| `08-phases.md` | Phase 0–4 task breakdown with exit criteria |

---

## 1. Goals

1. **Pure C11 ergonomics.** A C developer can `#include <aegon/c/aegon.h>`, link
   `-laegon_c`, and serve HTTP/1.1 + HTTP/2 + HTTP/3 + WebSocket with no C++ toolchain
   knowledge and no C++ types leaking into their translation units.
2. **FFI-first shape.** Every function is trivially bindable: opaque pointers,
   `{ptr, len}` string slices, plain-enums, no exceptions, no templates, no `std::*`
   in public headers, no struct-by-value across ownership boundaries except the
   16-byte `aegon_str_t`.
3. **Zero-compromise performance.** The C API sits directly on Aegon's hot paths:
   request data is returned as zero-copy slices into the connection buffer pool, and
   synchronous C handlers execute through `Router::dispatch_sync()` — zero coroutine
   frames, zero heap allocations per request.
4. **Stable, versioned ABI.** Append-only within a major version, `struct_size`
   versioning on all configuration aggregates, explicit symbol visibility.

## 2. Non-Goals

- **Dynamically loaded route plugins** (dlopen'd route providers) — explicitly out of scope.
- **C bindings for data modules** (`aegon_redis`, `aegon_orm`, `aegon_memory`) — deferred;
  will be revisited once the HTTP surface proves itself (see `08-phases.md`, Phase 4+).
- **Typed/template features in C.** Glaze-reflected `bind_json<T>`, typed
  `ServiceRegistry::get<T>`, typed gRPC codecs, and the JWT template API have no C
  equivalent. The C ABI exposes the raw-bytes equivalents instead.
- **C++ coroutine interop.** C handlers are synchronous by default; asynchronous FFI
  is provided through an explicit completion-token bridge (`06-api-streaming.md`, `async.h`).

## 3. Scope

In scope (wraps existing C++ 1:1 unless noted):

| Module | C++ source | C header(s) | Phase |
| :--- | :--- | :--- | :--- |
| Types, errors, version, strings | `src/Version.h`, `src/http/Protocol.h` | `types.h`, `error.h`, `aegon.h` | 0–1 |
| Logging | `src/log/*` | `log.h` | 1 |
| Request inspection | `src/http/Request.h`, `HeaderMap.h` | `request.h` | 1 |
| Response mutation | `src/http/Response.h`, `Cookie.h` | `response.h` | 1 |
| Per-request context | `src/http/Context.h` | `context.h` | 1 |
| Routing & groups | `src/http/Router.h`, `RouteGroup.h` | `router.h` | 1 |
| Server lifecycle & config | `src/http/Server.h`, `ServerConfig.h` | `server.h` | 1 |
| Custom + built-in middleware | `src/http/Middleware.h`, `middleware/*` | `middleware.h`, `cors.h`, `compress.h`, `security.h`, `access_log.h`, `auth.h` | 2 |
| Static files | `Router::static_files`, `RouteGroup.h` | `static_files.h` | 2 |
| Multipart parsing | `src/http/MultipartParser.h` | `multipart.h` | 2 |
| Async FFI completion bridge | new (uses `EventLoop` + `IoEngine::wakeup`) | `async.h` | 3 |
| WebSocket | `src/http/websocket/*` | `websocket.h` | 3 |
| Server-Sent Events | `src/http/SseStream.h` | `sse.h` | 3 |
| gRPC (raw payload model) | `src/http/grpc/*` | `grpc.h` | 4 |

## 4. Design Principles (summary — see `01-architecture.md`)

1. **`aegon_str_t { const char* data; size_t len; }`** is the only string type crossing
   the boundary in the Aegon→C direction. Request strings are **not NUL-terminated**
   (they are `std::string_view` slices into `BufferPool` memory). C-string conveniences
   (`AEGON_STR`, `aegon_str_dup`, `aegon_mem_free`) are provided.
2. **Safe-by-default writes, explicit zero-copy opt-in.** `aegon_res_body/text/json/header`
   copy into `Response`'s owned storage. `*_static` variants map to
   `Response::body_view()` / `Response::header()` and require the caller to guarantee
   lifetime until the response is serialized.
3. **Opaque handles are zero-cost casts.** `aegon_ctx_t*` *is* `aegon::http::Context*`,
   `aegon_req_t*` *is* `aegon::http::Request*`, etc. No per-request wrapper allocation.
4. **Exceptions never cross the boundary.** Every entry point is wrapped in a
   try/catch firewall; failures return `aegon_err_t` and publish a thread-local
   message retrievable via `aegon_last_error()`.
5. **Synchronous handlers stay on the fast path.** `aegon_handler_fn` registers a C++
   `SyncHandler`, preserving `Router::dispatch_sync()` (zero-alloc) dispatch.
6. **Configuration is copy-in.** All config structs carry `size_t struct_size` as their
   first field and are fully copied at the call site; the user may stack-allocate them.

## 5. Naming & Signature Conventions

- Prefix: `aegon_`. Handles end in `_t` and are opaque (`typedef struct aegon_server aegon_server_t;`).
- Fallible calls return `aegon_err_t` (`AEGON_OK == 0`). Constructors return pointers
  (`NULL` on failure, check `aegon_last_error()`).
- Length-bearing C→Aegon inputs use `aegon_str_t` **by value** (16-byte register-passed
  struct on SysV; uniformly safe with one shared header on all tier-1 platforms).
  Setup-time path/pattern inputs use plain NUL-terminated `const char*`.
- Every registration function accepting `void* userdata` also accepts an optional
  `aegon_cleanup_fn` invoked exactly once when the registration is torn down.
- Functions documented **thread-safe**: `aegon_server_stop`, `aegon_async_complete`,
  `aegon_sse_send_*` / `aegon_sse_close`, `aegon_last_error`. Everything else:
  configuration-phase calls are single-threaded; per-request calls run on the
  connection's worker thread and must not be used from other threads.

## 6. Target Layout

```text
include/aegon/c/
├── aegon.h            # umbrella: version macros + includes everything below
├── export.h           # AEGON_C_API visibility / dllexport, AEGON_C_BEGIN/END
├── types.h            # aegon_str_t, aegon_err_t, method/status/version enums
├── error.h            # aegon_last_error, aegon_err_str
├── log.h              # logging subsystem
├── request.h          # request inspection
├── response.h         # response mutation
├── context.h          # context glue, app data, per-request locals, problem details
├── router.h           # routes, groups, error/not-found/405 handlers
├── server.h           # server lifecycle + aegon_server_config_t
├── middleware.h       # custom C middleware
├── cors.h             # built-in CORS middleware            (Phase 2)
├── compress.h         # built-in gzip/deflate middleware    (Phase 2)
├── security.h         # built-in security headers           (Phase 2)
├── access_log.h       # built-in access log middleware      (Phase 2)
├── auth.h             # bearer/basic/api-key/cookie auth    (Phase 2)
├── static_files.h     # static file serving                 (Phase 2)
├── multipart.h        # multipart/form-data parsing         (Phase 2)
├── async.h            # async completion-token FFI bridge   (Phase 3)
├── websocket.h        # WebSocket                            (Phase 3)
├── sse.h              # Server-Sent Events                   (Phase 3)
└── grpc.h             # raw-payload gRPC                    (Phase 4)

src/c/
├── internal.h         # AEGON_C_TRY/CATCH firewall, cast helpers, holder structs
├── error.cpp  log.cpp  request.cpp  response.cpp  context.cpp
├── router.cpp  server.cpp
├── middleware.cpp  cors.cpp  compress.cpp  security.cpp  access_log.cpp  auth.cpp
├── static_files.cpp  multipart.cpp
├── async.cpp  websocket.cpp  sse.cpp  grpc.cpp

tests/c/               # pure C11 test executables (CTest)
examples/c/            # pure C examples (hello, rest, middleware, tls, ws, sse, grpc)
```

## 7. Phase Overview

| Phase | Deliverable | Exit criteria (full list in `08-phases.md`) |
| :--- | :--- | :--- |
| **0 — Skeleton** | `aegon_c` CMake target, export macros, error core, umbrella header | Builds static+shared on Linux/Windows; `aegon_version_string()` callable from C |
| **1 — HTTP MVP** | types, log, request, response, context, router, groups, server, TLS, lifecycle | Pure C "hello world" + REST JSON service pass tests; sync fast path verified (no coroutine frame) |
| **2 — Middleware & files** | custom middleware, CORS, compress, security, access log, auth, static files, multipart | Middleware chain ordering tests; static file + multipart examples pass |
| **3 — Real-time & async** | `EventLoop::post` core addition, async token bridge, WebSocket, SSE | WS echo + cross-thread async handler + SSE stream tests pass under ASAN/TSAN |
| **4 — gRPC & hardening** | raw-payload gRPC unary (+streaming via async bridge), packaging polish, fuzz boundary | grpc_unary example with nanopb passes; pkg-config/CPack artifacts verified |

Each API listing in files `02`–`06` is tagged with its phase in the margin
comment (`// Phase N`) so the headers can be implemented incrementally
without redesign.
