# 01 — Architecture

Binding decisions for the Aegon C ABI. Every API listing in `02`–`06` follows from
these rules; if an API ever needs to violate one, amend this file first.

---

## A1. String representation: `aegon_str_t`

```c
typedef struct aegon_str {
    const char* data;   /* NOT necessarily NUL-terminated; may be NULL iff len == 0 */
    size_t      len;
} aegon_str_t;
```

**Why.** Aegon's request surface (`Request::path()`, `query()`, `body()`, `param()`,
`HeaderMap` entries) returns `std::string_view` slices pointing directly into the
connection's `BufferPool` / `io_uring` provided-buffer ring. They are not NUL-terminated,
and copying them just to fabricate a `const char*` would destroy the zero-allocation
hot path that makes Aegon fast (the `baseline-h2c` profile at 14.4M req/s depends on it).

**Rules.**
1. Aegon→C direction: always `aegon_str_t` by value. Valid until the handler returns
   (see A5 lifetime table). `data == NULL` with `len == 0` represents "absent/empty".
2. C→Aegon direction:
   - Setup/registration-time inputs (route patterns, listen host, cert paths, log file
     path) use plain `const char*` NUL-terminated strings — these are called once,
     allocation cost is irrelevant, and C ergonomics win.
   - Hot-path inputs (response bodies, header values, SSE events) use `aegon_str_t`
     **by value**. A 16-byte struct return/pass is register-allocated on System V AMD64
     (rax:rdx) and uniform across Windows x64 / AArch64, which keeps one shared header
     legal on every tier-1 platform.
3. Ergonomic helpers (defined in `types.h`):
   - `AEGON_STR("literal")` — compile-time `aegon_str_t` from a string literal.
   - `aegon_str(const char* cstr)` — runtime wrap of a NUL-terminated string.
   - `aegon_str_dup(aegon_str_t)` / `aegon_mem_free(void*)` — heap copy into an
     Aegon-allocated NUL-terminated buffer; freeing pairs with `aegon_mem_free` so C
     consumers never call the wrong runtime's `free` (critical on Windows, where
     `libaegon_c.dll` may use a different CRT than the host app).

## A2. Response writes: owned by default, view by explicit opt-in

`aegon::http::Response` has two storage modes:
- **Owned**: `body()`, `text()`, `json()`, `set_header_owned()` — data is copied into
  the response and survives handler return.
- **View**: `body_view()`, `header()` — stores a `std::string_view`; the memory must
  outlive response serialization.

C/FFI callers routinely pass stack buffers or GC-managed memory that dies when the
handler returns. Therefore:

| C function | C++ mapping | Lifetime requirement |
| :--- | :--- | :--- |
| `aegon_res_body/text/json/html/header` | `body()/text()/json()/set_header_owned()` | none — copied |
| `aegon_res_body_static` | `body_view()` | caller guarantees buffer outlives serialization |
| `aegon_res_header_static` | `header()` | name+value must be compile-time/pinned |

The default set is always safe. The `_static` set exists so benchmark-grade services
(whose payloads are string literals or pinned arenas) keep the zero-copy property.

## A3. Handle identity — zero-cost casts

```text
aegon_ctx_t*    ≡  aegon::http::Context*        (per-request, never heap-wrapped)
aegon_req_t*    ≡  aegon::http::Request*        (borrowed from ctx)
aegon_res_t*    ≡  aegon::http::Response*       (borrowed from ctx)
aegon_server_t* ≡  aegon::http::Server*         (owned, created/destroyed by user)
aegon_router_t* ≡  aegon::http::Router*         (owned, moved into server)
aegon_group_t*  →  heap holder of aegon::http::RouteGroup (by-value C++ class)
aegon_next_t*   →  stack holder of MiddlewareFn chain state (middleware-call scoped)
aegon_async_t*  →  heap holder: EventLoop*, coroutine_handle, user ctx, refcount
aegon_ws_t*     ≡  aegon::http::websocket::WebSocket* (session-scoped)
aegon_sse_t*    →  heap holder: connection write channel + open flag, thread-safe
```

All handles are opaque (`typedef struct X X_t;`) in the public headers. The casts
live in `src/c/internal.h`. **No wrapper allocation occurs on any per-request path** —
`aegon_ctx_req()`/`aegon_ctx_res()` are pure `reinterpret_cast`s.

## A4. Error model — exception firewall

C++ exceptions must never unwind across `extern "C"`. Every C entry point body is:

```cpp
aegon_err_t aegon_server_listen(...) noexcept {
    AEGON_C_TRY {
        // ... real work ...
        return AEGON_OK;
    } AEGON_C_CATCH;   // catches std::exception& and ..., records thread-local message
}
```

- `AEGON_C_CATCH` maps the exception to an `aegon_err_t` and stores
  `e.what()` (plus file/line in debug builds) in a `thread_local` buffer.
- `aegon_last_error()` returns that thread's last message (NUL-terminated, owned by
  the library, overwritten by the next error **on the same thread**).
- `aegon_err_str(err)` returns a static, thread-safe description of an error code.
- Enum values are permanently assigned (see `02-api-core.md`); new codes append.

## A5. Object lifetimes & ownership

| Object | Created by | Destroyed by | Validity |
| :--- | :--- | :--- | :--- |
| `aegon_server_t` | `aegon_server_create*` | `aegon_server_destroy` | user-owned; must not be running |
| `aegon_router_t` | `aegon_router_create` | `aegon_router_destroy` **or** consumed by `aegon_server_create` (NULLs caller's pointer) | single ownership, move semantics |
| `aegon_group_t` | `aegon_router_group*` | `aegon_group_destroy` | must be destroyed **before** its router is consumed |
| `aegon_ctx_t` | framework, per request | framework | handler (or middleware) invocation only; **never stored** unless handed to `aegon_async_create` |
| `aegon_req_t`/`aegon_res_t` | `aegon_ctx_req/res` | framework | same scope as ctx |
| Request string slices | `aegon_req_*` getters | framework | until handler/middleware returns |
| `aegon_cookie_options_t` | user | call-site copy | copied fully by `aegon_res_set_cookie` |
| `aegon_multipart_t` | `aegon_multipart_parse` | `aegon_multipart_destroy` | user-owned; fields point into body slice which must stay alive |
| `aegon_ws_t` | framework per WS session | framework | from `on_open` until `on_close` returns |
| `aegon_sse_t` | `aegon_sse_create` | `aegon_sse_destroy` | user-owned, cross-thread safe (see A6) |
| `aegon_async_t` | `aegon_async_create` | framework after completion callback runs | one-shot |
| Registration `userdata` | user | user's `aegon_cleanup_fn`, invoked once at teardown | router/group/middleware lifetime |

## A6. Threading model

Aegon is **thread-per-core, shared-nothing**: `Server::run(n)` spawns `n` workers,
each with its own `EventLoop` (io_uring/epoll/IOCP). There are no cross-thread locks
on the request path. The C ABI respects this:

1. **Configuration phase is single-threaded.** All `aegon_server_*` setters, all
   `aegon_router_*`/`aegon_group_*` registration calls must happen on one thread
   before `aegon_server_run*`.
2. **Per-request calls are thread-confined.** `aegon_ctx_*`, `aegon_req_*`,
   `aegon_res_*` may only be called on the worker thread executing the handler.
3. **Explicitly thread-safe calls** (safe from any thread):
   - `aegon_server_stop(server)` — mirrors `Server::stop()` (atomic + `wakeup()`).
   - `aegon_async_complete(token)` — wakes the owning loop (Phase 3 adds
     `EventLoop::post`, see `08-phases.md`).
   - `aegon_sse_send_*`, `aegon_sse_close` — internally bounce the write onto the
     owning loop via the same mechanism.
   - `aegon_last_error()` — thread-local by construction.
4. **`aegon_server_run` vs `aegon_server_run_async`.** `run` blocks the calling
   thread for the process lifetime; `run_async` spawns a supervisor thread and returns,
   enabling FFI hosts (Python, Node, Go) that own their main thread.
5. **One handler invocation = one logical execution.** C handlers are synchronous.
   A handler that needs to do slow/async work (FFI into another runtime, blocking
   driver call) must use the `async.h` completion bridge; it must **never** block the
   worker thread.

## A7. Coroutine fast path preservation

`Router::dispatch_sync()` executes `RouteHandler::Kind::Sync` handlers inline:
zero coroutine frames, zero heap allocations. The C wrapper registers its lambda as a
**synchronous** `void(Context&)` handler:

```cpp
router->add_route(method, pattern, [fn, ud](Context& ctx) { fn(&ctx, ud); });
//                                       ^^ converts to SyncHandler via detail::make_handler
```

Consequences:
- `aegon_router_add/get/post/...` handlers get the full fast path automatically.
- **Any** `aegon_router_use*()` custom middleware forces the asynchronous dispatch
  path globally (this mirrors C++ `Router::dispatch_sync`'s `global_middleware_.empty()`
  check) and is documented as such in `05-api-middleware.md`.
- The async bridge (`async.h`) is opt-in per route and wraps that route's handler in
  an `AsyncHandler` coroutine; un-wrapped routes are unaffected.

## A8. Configuration versioning

Every C-visible configuration aggregate begins with `size_t struct_size`, initialized
by a dedicated `aegon_*_init(cfg)` function (or by assignment from
`AEGON_SERVER_CONFIG_DEFAULT`-style constants where a full constant is practical):

```c
aegon_server_config_t cfg;
aegon_server_config_init(&cfg);   /* zeroes, sets struct_size, applies C++ defaults */
cfg.ring_entries = 8192;
```

- The C++ side reads **only** fields covered by `min(cfg->struct_size, sizeof(cpp-side mirror))`,
  treating the rest as defaulted.
- New fields are always appended; reordered/removed fields never happen.
- This is the classic `cbSize` pattern (Win32, Vulkan) — proven, FFI-friendly, and
  compatible with C89-style partial designated initialization.

## A9. ABI stability policy

- **Append-only within a major version.** Functions are never removed or re-signed;
  enums only gain values; structs only gain trailing fields.
- **Symbol visibility is explicit.** Only `AEGON_C_API` functions are exported.
  On Linux a linker version script additionally pins the export list; on Windows
  `__declspec(dllexport)`. The static build (`libaegon_c.a`) defines
  `AEGON_C_STATIC` which empties the decoration.
- **`aegon_abi_version()`** returns the ABI epoch (starts at 1). Bumped only on a
  breaking change, which also bumps the SONAME.
- **No C++ types in public headers.** CI compiles every public header with
  `gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only` and with clang-cl/MSVC
  headers-only TU to guarantee C11 cleanliness.
- **No exceptions/RTTI assumptions across the boundary.** The C++ side may build with
  `-fno-exceptions` in the future; the firewall macro already isolates this decision.

## A10. C++ types that intentionally have no C equivalent

| C++ feature | Reason for exclusion | C escape hatch |
| :--- | :--- | :--- |
| `ctx.bind_json<T>()` (Glaze reflection) | Templates; C has no reflection | `aegon_req_body()` + caller's JSON library (cJSON, yyjson, jsmn); documented in examples |
| `Server::provide<T>` / `ctx.service<T>()` | `std::type_index`-keyed registry | `aegon_server_set_app_data` / `aegon_ctx_app_data` (single void* — sufficient for C; named slots via a user-side hash map) |
| `ctx.set<T>/get<T>` per-request bag | `typeid`-keyed | `aegon_ctx_local_set/get` (string-keyed) |
| `middleware::auth<T>::validator → Task<optional<T>>` | typed principal into `ctx.set<T>` | validator returns `bool` + user stores principal via `aegon_ctx_local_set` |
| `grpc_service<T>().unary<Req,Res>` | protobuf codegen types | `grpc.h` raw-payload model: caller owns (de)serialization (nanopb, protobuf-c) |
| JWT template API (`JwtVerifier<T>`) | typed claims | Phase 4+: possible untyped `jwt.h` exposing claims JSON; not in initial scope |
| gRPC compressed frames | `GrpcCodec` doesn't support them yet | inherit limitation; documented |

## A11. Performance guardrails

1. Request inspection getters never allocate — they wrap existing `string_view`s.
2. `aegon_req_header_at/count` walk the inline `HeaderMap` storage directly
   (≤ 32 inline entries), no allocation.
3. `aegon_router_add*` performs route pattern normalization exactly as C++ does —
   patterns are consumed once at registration.
4. `aegon_last_error` allocates at most once per thread (buffer reuse).
5. The C wrapper translation unit adds **zero** includes of glaze/openssl/nghttp2 to
   consumer builds; those stay private link dependencies of `aegon_c`.
6. Benchmarks: Phase 1 exit requires a C `hello_world` server within **2%** of the
   equivalent C++ sync-handler server on the `baseline` profile (same box, same flags).
   This is the standing regression gate for every subsequent phase.
