# 08 — Phased Execution Plan

Each phase lists concrete tasks, its public API surface (files `02`–`06` carry the
full signatures), and **exit criteria** that must hold before the next phase starts.
Phase 0 and Phase 1 can be one PR each; Phases 2–4 may be split per header.

---

## Phase 0 — Skeleton & Infrastructure

**Goal:** a buildable, installable, empty-but-real `libaegon_c`.

### Tasks

1. `include/aegon/c/export.h`, `types.h` (version macros, `aegon_str_t`, all enums,
   `AEGON_STR`), `error.h`, `aegon.h` (umbrella, version getters only).
2. `src/c/internal.h` — `AEGON_C_TRY`/`AEGON_C_CATCH`, thread-local error buffer,
   `record_error()`, cast helpers (`to_cpp(aegon_ctx_t*)` etc.).
3. `src/c/error.cpp` — `aegon_last_error`, `aegon_last_error_clear`, `aegon_err_str`,
   version getters, `aegon_abi_version`.
4. CMake: `AEGON_BUILD_C_ABI` option, `aegon_c` target, alias `Aegon::c`,
   install rules, version script `cmake/aegon_c.map`, export integration.
5. `tests/c/test_core.c` + CTest registration; header-hygiene script
   (`cmake/CheckCHeaders.cmake` or CI inline).
6. pkg-config template `cmake/aegon-c.pc.in`.

### Exit criteria

- [ ] `cmake -B build -DAEGON_BUILD_C_ABI=ON` builds shared & static on Linux and Windows.
- [ ] A pure C11 TU including `aegon/c/aegon.h` compiles with `-std=c11 -Werror -pedantic` (gcc, clang) and MSVC `/std:c11 /W4 /WX`.
- [ ] `nm -D` on `libaegon_c.so` exports only `aegon_*`.
- [ ] `c_test_core` passes; `aegon_version_string()` returns the `Version.h` string.
- [ ] `pkg-config --cflags --libs aegon-c` works from a staged install.

---

## Phase 1 — HTTP Server MVP

**Goal:** a C developer can write, build, and run a real REST service.

### Surface delivered

`types.h` (remainder), `error.h`, `log.h`, `request.h`, `response.h`, `context.h`,
`router.h`, `server.h` (minus Phase-3 hooks), umbrella includes.

### Tasks

1. `src/c/log.cpp` — logger init/level/sinks/write/flush/shutdown.
2. `src/c/request.cpp` — all getters; `header_at`/`param_at` iteration;
   `aegon_url_decode`; method/status/version string helpers.
3. `src/c/response.cpp` — status, owned/static headers & bodies, text/json/html,
   cookie options init + set/clear, file + MIME, redirect, trailers.
4. `src/c/context.cpp` — req/res accessors, app_data passthrough, string-keyed
   per-request locals (backed by a lazily-created map hung off `Context`'s
   `local_store_` via a stable key, or a small parallel map in the wrapper — pick
   the zero-per-request-cost option: only allocate on first `local_set`),
   `problem`, `send_file`.
5. `src/c/router.cpp` — create/destroy; `add` + verb wrappers registering
   **SyncHandler** lambdas (`01-architecture.md` §A7); error/404/405 handlers
   (C++ error path wrapped so C handler receives `what()`); groups create/nested/
   destroy + group route registration; userdata + cleanup bookkeeping
   (`std::shared_ptr` deleter trick so cleanups fire exactly once at router teardown).
6. `src/c/server.cpp` — config init/mapping into `ServerConfig`+`EngineConfig`
   (struct_size-aware field copy), create (router consume semantics), listen,
   listen_tls, enable_tls, app_data (stored via `ServiceRegistry::register_service`
   with a stable wrapper type), run/run_async/stop/join/is_running.
   `run_async` supervisor: `std::thread` owned by the server handle.
7. Tests: `test_core.c` (finish), `test_log.c`, `test_request.c`, `test_response.c`,
   `test_router.c`, `test_group.c`, `test_server.c`; fixture server helper.
8. Examples: `hello_world`, `rest_json`, `tls_server`.
9. Benchmark gate: `benchmarks/` C `hello_world` vs C++ sync baseline.

### Exit criteria

- [ ] All Phase-1 tests green on Linux + Windows, ASAN clean.
- [ ] `rest_json` example: `curl` CRUD over params/query/JSON body works; 404/405/problem+json bodies correct.
- [ ] Sync fast path proven: route registered from C dispatches via `Router::dispatch_sync` (asserted in a unit test using a sync-only router with no middleware; perf gate within 2% of C++ baseline).
- [ ] h1 + h2c + TLS(h2 ALPN) verified against `curl --http2-prior-knowledge` and `openssl s_client -alpn h2`.
- [ ] No public header includes any C++ header; static + shared builds both pass.

---

## Phase 2 — Middleware, Static Files, Multipart

**Goal:** production middleware surface parity with the C++ built-ins.

### Surface delivered

`middleware.h`, `cors.h`, `compress.h`, `security.h`, `access_log.h`, `auth.h`,
`static_files.h`, `multipart.h`.

### Tasks

1. `middleware.cpp` — C `MiddlewareFn` adapter + stack `aegon_next_t` holder;
   router/group `use`; per-route slot arrays (`add_mw`).
2. `cors.cpp`, `compress.cpp`, `security.cpp`, `access_log.cpp` — config struct
   mapping into the C++ aggregates; string-array copying; C callback adapters for
   `skip_if`/`custom_formatter` (access log) with the fixed-buffer contract.
3. `auth.cpp` — bearer/basic/api-key/cookie validators as `MiddlewareFn` adapters;
   401 problem+json default (matches C++ `default_send_unauthorized`).
4. `static_files.cpp` — config mapping; cache-control callback adapter.
5. `multipart.cpp` — parse/destroy + field/file lookup & iteration.
6. Tests: `test_middleware.c`, `test_cors.c`, `test_compress.c`, `test_security.c`,
   `test_access_log.c`, `test_auth.c`, `test_static_files.c`, `test_multipart.c`.
7. Examples: `middleware_chain`, `static_site`, `upload`.
8. Docs: middleware ordering semantics; fast-path interaction warning
   (global `use` disables `dispatch_sync` — same as C++).

### Exit criteria

- [ ] All Phase-2 tests green incl. ASAN.
- [ ] Middleware onion order verified across global→group→route with pre+post logging.
- [ ] CORS preflight, compression negotiation, security header set, and auth 401s byte-verified against expected wire output.
- [ ] `static_site` serves a fixture tree with etag 304 revalidation; `upload` round-trips a binary file byte-exact.

---

## Phase 3 — Real-time & Async Bridge

**Goal:** WebSocket, SSE, and cross-thread asynchronous handlers.

### Prerequisite C++ change (reviewed separately)

- Add `core::EventLoop::post(std::function<void()>)` — thread-safe task injection
  using `IoEngine::wakeup`; per-engine implementation (io_uring MPSC + wakeup,
  epoll eventfd, IOCP `PostQueuedCompletionStatus`). Small, additive, useful to
  pure-C++ users; lands with unit tests in `aegon_core`.

### Surface delivered

`async.h`, `websocket.h`, `sse.h`; `server.h` hooks (`on_start`, `on_stop`,
`spawn_worker`).

### Tasks

1. `async.cpp` — `aegon_async_t` holder (EventLoop*, coroutine handle, done cb,
   userdata, atomic-completed guard); `aegon_router_add_async`/`aegon_group_add_async`
   registering `AsyncHandler` coroutines that suspend after invoking the C handler;
   `aegon_async_complete` → `loop->post(...)` resume.
2. `websocket.cpp` — `Router::ws` registration bridging `WebSocket` callbacks;
   synchronous-from-C send wrappers (wrapper co_awaits before next frame dispatch);
   close/open state; `ws_echo`.
3. `sse.cpp` — `aegon_sse_create` inside async routes (capture `SseWriteFn` via the
   `SseProvider` injected by the connection layer); thread-safe send/close via
   `EventLoop::post`; on_close callback wiring; destroy semantics.
4. Server hooks: `on_start`/`on_stop` lifecycle callbacks; `spawn_worker` bridge
   (C worker driven by async primitives; contract documented in header).
5. Tests: `test_async.c` (incl. TSAN run, owner-thread assertion, double-complete
   guard), `test_websocket.c`, `test_sse.c`.
6. Examples: `async_bridge`, `ws_echo`, `sse_feed`.

### Exit criteria

- [ ] Async bridge: 10k concurrent detached requests, completions from 8 foreign threads, zero lost/stuck requests, ASAN+TSAN clean.
- [ ] WebSocket: RFC 6455 echo incl. fragmentation, ping/pong, close handshake codes; 426 fallback intact.
- [ ] SSE: events sent from a non-worker thread arrive framed and ordered on h1 (chunked), h2, and h3; close sends terminal chunk; reconnect-safe.
- [ ] `spawn_worker` survives graceful stop without hangs (ctest timeout guard).

---

## Phase 4 — gRPC & Packaging Hardening

**Goal:** raw-payload gRPC parity with the C++ implementation, plus release polish.

### Surface delivered

`grpc.h`.

### Tasks

1. `grpc.cpp` — raw-codec specialization of the unary path (`GrpcCodec` for raw
   bytes: strip/add 5-byte prefix, no protobuf dependency); reply/error → trailers;
   service prefix + method registration.
2. Streaming RPCs: server/client/bidi via `GrpcInboundChannel` + async bridge
   callbacks (on_message/on_half_close/on_close; send/error senders).
3. `test_grpc.c` — hand-framed unary round-trip; error trailers; UNIMPLEMENTED for
   unregistered method; client-streaming accumulation flow.
4. `examples/c/grpc_unary/` with nanopb-generated messages.
5. Packaging hardening:
   - verify `Libs.private` completeness for the static archive on a clean container;
   - CPack DEB/RPM component split (`aegon-c-dev`, `aegon-c-lib`);
   - fuzz the C boundary: `aegon_str_t` with embedded NULs, huge lens, NULL data
     with non-zero len (must be rejected with `ERR_INVALID_ARG`, never crash) —
     extend existing `build-fuzz` infra with a C-API harness.
6. Docs: gRPC raw-payload guide (framing, status codes, nanopb/protobuf-c recipes);
   finalize the "C API" docs section; README quickstart.

### Exit criteria

- [ ] grpcurl-compatible unary call against `grpc_unary` example succeeds (h2, TLS); error status path returns correct trailers-only response.
- [ ] Streaming: server-streaming 1→N and bidi N→N message flows byte-verified.
- [ ] Clean-container static-link smoke test passes with only `pkg-config` output.
- [ ] Fuzz harness: 24h boundary run, no crashes, all rejections are clean `aegon_err_t`s.

---

## Post-Phase-4 candidates (not committed)

- Data module bindings (`aegon_redis`, `aegon_memory`, `aegon_orm` query builder) —
  gated on HTTP surface adoption.
- Untyped JWT helper header (`jwt.h`) exposing claims as JSON.
- HTTP **client** C API (`aegon_http_client_*`).
- Reference bindings: Rust crate (`bindgen` + safe wrapper), Python package
  (`ctypes`/Cython), Zig package — living in separate repos.
