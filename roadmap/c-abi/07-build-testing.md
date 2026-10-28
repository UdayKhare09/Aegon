# 07 — Build, Packaging, Testing, CI

---

## 7.1 CMake integration

### New option

```cmake
option(AEGON_BUILD_C_ABI "Build the C ABI wrapper library (libaegon_c)" ON)
```

### Target definition (root `CMakeLists.txt`, after `aegon_http`)

```cmake
if(AEGON_BUILD_C_ABI)
    add_library(aegon_c
        src/c/error.cpp
        src/c/log.cpp
        src/c/request.cpp
        src/c/response.cpp
        src/c/context.cpp
        src/c/router.cpp
        src/c/server.cpp
        # Phase 2: middleware.cpp cors.cpp compress.cpp security.cpp
        #          access_log.cpp auth.cpp static_files.cpp multipart.cpp
        # Phase 3: async.cpp websocket.cpp sse.cpp
        # Phase 4: grpc.cpp
    )
    add_library(Aegon::c ALIAS aegon_c)

    target_link_libraries(aegon_c PRIVATE aegon_http)
    # ^ PRIVATE: consumers of libaegon_c need no C++ headers, no glaze/openssl/
    #   nghttp2 include dirs. For the SHARED build this fully encapsulates the
    #   C++ engine; for the STATIC build, consumers link transitively via the
    #   generated pkg-config/CMake config which lists the private deps.

    target_compile_definitions(aegon_c PRIVATE AEGON_C_BUILDING)

    # Consumer defines AEGON_C_STATIC when linking the static archive.
    target_compile_definitions(aegon_c PUBLIC
        $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:AEGON_C_STATIC>)

    set_target_properties(aegon_c PROPERTIES
        EXPORT_NAME c
        VERSION   ${AEGON_VERSION_STRING}
        SOVERSION 1                     # == AEGON_ABI_VERSION epoch
        C_VISIBILITY_PRESET     hidden  # belt-and-braces with the version script
        CXX_VISIBILITY_PRESET   hidden
        VISIBILITY_INLINES_HIDDEN ON
    )

    target_include_directories(aegon_c PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
endif()
```

Build both variants in CI: `-DBUILD_SHARED_LIBS=ON` (produces
`libaegon_c.so.1.x.y` / `aegon_c.dll`) and `OFF` (produces `libaegon_c.a`).

### Export & install (`cmake/InstallAndExport.cmake` additions)

- Append `aegon_c` to `AEGON_ACTIVE_TARGETS` (same `EXPORT AegonTargets`,
  `Aegon::c` namespace alias).
- Install public headers **from `include/`** (not `src/`):

```cmake
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/aegon/c
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aegon
        FILES_MATCHING PATTERN "*.h")
```

### Version script (`cmake/aegon_c.map`) — Linux shared builds

```text
AEGON_C_1.0 {
    global:
        aegon_*;
    local:
        *;
};
```

```cmake
if(UNIX AND NOT APPLE AND BUILD_SHARED_LIBS)
    target_link_options(aegon_c PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/aegon_c.map")
endif()
```

### pkg-config (`cmake/aegon-c.pc.in`)

```text
prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@
includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@

Name: aegon-c
Description: C ABI for the Aegon high-performance HTTP engine
Version: @AEGON_VERSION_STRING@
Libs: -L${libdir} -laegon_c
Libs.private: @AEGON_C_PRIVATE_LIBS@   # aegon_http aegon_core aegon_log + uring, ssl, crypto, nghttp2...
Cflags: -I${includedir}
```

Generated via `configure_file` + installed to `${CMAKE_INSTALL_LIBDIR}/pkgconfig`.

---

## 7.2 Public-header hygiene gate

The entire C ABI contract rests on headers being valid C11. Enforce in CI (and
locally via `cmake --build build --target c-abi-header-check`):

```bash
# GCC / Clang — pure C11, no extensions
for h in include/aegon/c/*.h; do
    cc  -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -Iinclude "$h"
done

# MSVC headers-only TU (a .c file that #includes every public header)
cl /std:c11 /W4 /WX /c tests/c/all_headers.c
```

Rules this enforces: no `//`-only headers relying on C++ linkage, no C++ keywords,
no `std::*` includes, `noexcept` only inside `AEGON_C_BEGIN/END`-compatible form
(it is **not** valid C — so public signatures omit it; the `.cpp` definitions carry it).

> ⚠ **Correction to listings in `02`–`06`:** the `noexcept` tokens shown on
> declarations are documentation of intent; the actual headers must drop them
> (C11 has no `noexcept`). The C++ definitions in `src/c/*.cpp` are all `noexcept`.

---

## 7.3 Test suite (`tests/c/`)

Pure C translation units, compiled as C, linked against `aegon_c`, registered with
CTest. Test harness: a minimal `check.h`-style header (assert macros), no external
test framework — keeps `tests/c` dependency-free.

### Test matrix

| File | Phase | Coverage |
| :--- | :--- | :--- |
| `test_core.c` | 0–1 | version/abi getters, `aegon_str`, `str_dup`/`mem_free` round-trip, `err_str`, `last_error` set/clear, method/status/version string round-trips |
| `test_log.c` | 1 | init (twice → `ERR_STATE`), level get/set, file sink writes a line, flush/shutdown |
| `test_request.c` | 1 | fixture server on ephemeral port: path/query/body/header/param/cookie getters; `header_at` iteration order; `url_decode` |
| `test_response.c` | 1 | status, copying vs static headers/bodies (static with literal), text/json/html content-types, cookies (attributes serialized), redirect, file 404 + file 200 with inferred MIME, trailers on h2 |
| `test_router.c` | 1 | static route hit, `:param` extraction, wildcard, trailing-slash normalization, 404 default + custom, 405 default + custom + `Allow` header, method NOT_ALLOWED mask |
| `test_group.c` | 1 | group prefix join, nested groups, group cleanup ordering |
| `test_server.c` | 1 | config init defaults, invalid port → `ERR_INVALID_ARG`, listen+run on threads=2, stop from another thread, run_async + join, app_data round-trip via ctx |
| `test_middleware.c` | 2 | global mw ordering (pre/post), short-circuit, group-scoped mw, per-route mw slots, cleanup invocation counts |
| `test_cors.c` | 2 | preflight 204 short-circuit, wildcard origin, credentials reflection, disallowed origin |
| `test_compress.c` | 2 | gzip + deflate negotiation, min_size skip, Vary merge |
| `test_security.c` | 2 | default header set present, HSTS string, CSP report-only name |
| `test_access_log.c` | 2 | log line emitted via memory sink; slow-threshold level bump; skip_paths |
| `test_auth.c` | 2 | bearer ok/401, basic base64 path, api-key header, cookie auth, `WWW-Authenticate` |
| `test_static_files.c` | 2 | serves fixture file, index resolution, etag revalidation 304, SPA fallback |
| `test_multipart.c` | 2 | two-field + one-file form round-trip, zero-copy data equality, malformed boundary → NULL |
| `test_async.c` | 3 | handler detaches, worker unblocked (second request served while first pends), completion from foreign thread, done-callback runs on owner thread (assert thread id), double-complete guarded |
| `test_websocket.c` | 3 | handshake, text echo round-trip, binary, ping/pong, close code/reason, 426 fallback on plain GET |
| `test_sse.c` | 3 | headers set, event/data/comment/retry frames arrive in order from foreign-thread sender, close terminates stream |
| `test_grpc.c` | 4 | unary call with hand-framed request (5-byte prefix) → framed reply, grpc-status 0; error path trailers-only; unknown method → 12 UNIMPLEMENTED |

### Test fixtures

- `tests/c/fixture_server.h`: spins a server on port 0-bound ephemeral port in
  `run_async` mode, exposes its port, stops in teardown.
- Client side: raw BSD sockets for HTTP/1.1 (keeps tests dependency-free);
  `nghttp2`-based mini-client for h2 trailer test (already a project dependency);
  raw-socket WebSocket frames (hand-rolled, ~80 lines).
- Sanitizer runs: the existing `build-asan` preset gains `tests/c` coverage;
  `test_async.c` additionally runs under TSAN.

---

## 7.4 Examples (`examples/c/`)

| Example | Phase | Demonstrates |
| :--- | :--- | :--- |
| `hello_world/` | 1 | minimal: router, one GET, run |
| `rest_json/` | 1 | params, query, JSON body echo (cJSON), problem details, status codes |
| `tls_server/` | 1 | `enable_tls` + `listen_tls`, h2 ALPN negotiation |
| `middleware_chain/` | 2 | custom logger mw + CORS + security headers + per-route auth |
| `static_site/` | 2 | static files with SPA fallback |
| `upload/` | 2 | multipart form with file save |
| `async_bridge/` | 3 | thread-pool offload of a blocking computation |
| `ws_echo/` | 3 | full-callback WebSocket chat-style echo |
| `sse_feed/` | 3 | timer-thread-driven SSE event feed |
| `grpc_unary/` | 4 | nanopb-generated messages over `aegon_router_grpc_unary` |

Each example is a single `main.c` + its own `CMakeLists.txt` gated behind
`option(AEGON_BUILD_C_EXAMPLES OFF)`.

---

## 7.5 CI additions (`.github/workflows`)

Extend the existing matrix with a `c-abi` job per platform (Linux gcc, Linux clang,
Windows MSVC):

1. Build `-DAEGON_BUILD_C_ABI=ON` shared + static.
2. Run header hygiene gate (§7.2).
3. `ctest -R '^c_' --output-on-failure` (all `tests/c` binaries prefixed `c_`).
4. Symbol-audit the shared object: `nm -D --defined-only libaegon_c.so | grep -v aegon_` must be empty (except libc/libstdc++ weak refs); on Windows verify exports via `dumpbin /EXPORTS`.
5. pkg-config smoke test: install to a staging prefix, compile `hello_world` example against it using only `pkg-config --cflags --libs aegon-c`.
6. Benchmark gate (Linux perf box, Phase 1 onward): C `hello_world` vs C++ sync
   baseline on the `baseline` profile — fail if C handler throughput is >2% below
   the C++ equivalent (validates the zero-wrapper-cost claim in `01-architecture.md` §A11).

---

## 7.6 Documentation deliverables

- `docs/` site section “C API”: quickstart (hello_world walkthrough), ownership &
  lifetime rules (digest of `01-architecture.md` §A5–A6), per-header reference
  generated from the roadmap listings, FFI binding notes (Python `ctypes` snippet,
  Rust `bindgen` allowlist: `aegon_.*`).
- README badge + C quickstart block once Phase 1 ships.
