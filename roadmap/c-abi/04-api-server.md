# 04 — Server API: `router.h`, `server.h`

Phase split: core routing + server lifecycle = **Phase 1**; `on_start`/`on_stop`/
`spawn_worker` (which require the async bridge machinery) = **Phase 3**.

Opaque handles:

```c
typedef struct aegon_router aegon_router_t;  /* ≡ aegon::http::Router  (owned)     */
typedef struct aegon_group  aegon_group_t;   /* holder of aegon::http::RouteGroup  */
typedef struct aegon_server aegon_server_t;  /* ≡ aegon::http::Server  (owned)     */
```

---

## 4.1 `include/aegon/c/router.h` — routing, groups, fallback handlers — **Phase 1**

C++ backing: `src/http/Router.h`, `RouteRegistrar.h`, `RouteGroup.h`.

### Handler model

```c
/* Synchronous handler: executes through Router::dispatch_sync() (zero coroutine
 * frames, zero heap allocations) — see 01-architecture.md §A7.
 * `ctx` and everything reachable from it is valid ONLY for the duration of the call. */
typedef void (*aegon_handler_fn)(aegon_ctx_t* ctx, void* userdata);

/* Invoked exactly once when the owning router/group/middleware registration is
 * torn down (router destroyed or consumed by server, server destroyed).
 * May be NULL. Order: reverse of registration, after server stop. */
typedef void (*aegon_cleanup_fn)(void* userdata);

/* Error handler (Router::ErrorHandler). Receives the last C++ exception message
 * (may be "" for non-std exceptions). Runs INSTEAD of the default RFC 7807 500. */
typedef void (*aegon_error_handler_fn)(aegon_ctx_t* ctx, aegon_str_t exception_what,
                                       void* userdata);
```

### Router lifecycle

```c
/* Creates an empty router. Destroy with aegon_router_destroy, or hand ownership to
 * aegon_server_create (which NULLs the caller's pointer). */
AEGON_C_API aegon_router_t* aegon_router_create(void) noexcept;                      /* P1 */
AEGON_C_API void            aegon_router_destroy(aegon_router_t* router) noexcept;   /* P1 */
```

### Route registration (Router::{get,post,put,del,patch,all,add_route})

All patterns follow Aegon syntax: static (`/health`), parameters (`/users/:id`),
wildcards (`/static/*`). Trailing-slash normalization matches C++
(`normalize_route_path`).

```c
/* Generic form — every verb convenience below forwards here. */
AEGON_C_API aegon_err_t aegon_router_add(aegon_router_t* router,
                                         aegon_method_t method,
                                         const char* pattern,
                                         aegon_handler_fn handler,
                                         void* userdata,
                                         aegon_cleanup_fn cleanup) noexcept;        /* P1 */

AEGON_C_API aegon_err_t aegon_router_get  (aegon_router_t*, const char* pattern, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_router_post (aegon_router_t*, const char* pattern, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_router_put  (aegon_router_t*, const char* pattern, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_router_del  (aegon_router_t*, const char* pattern, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_router_patch(aegon_router_t*, const char* pattern, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */

/* Registers handler for GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS
 * (RouteRegistrar::all — CONNECT/TRACE excluded, mirroring C++). */
AEGON_C_API aegon_err_t aegon_router_all  (aegon_router_t*, const char* pattern, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
```

### Fallback handlers (Router::{set_error_handler,set_not_found_handler,set_method_not_allowed_handler})

```c
AEGON_C_API aegon_err_t aegon_router_on_error(aegon_router_t* router,
                                              aegon_error_handler_fn handler,
                                              void* userdata,
                                              aegon_cleanup_fn cleanup) noexcept;    /* P1 */
AEGON_C_API aegon_err_t aegon_router_on_not_found(aegon_router_t* router,
                                                  aegon_handler_fn handler,
                                                  void* userdata,
                                                  aegon_cleanup_fn cleanup) noexcept;/* P1 */
AEGON_C_API aegon_err_t aegon_router_on_method_not_allowed(aegon_router_t* router,
                                                           aegon_handler_fn handler,
                                                           void* userdata,
                                                           aegon_cleanup_fn cleanup) noexcept; /* P1 */
```

### Route groups (Router::group / RouteGroup)

A group is a registration-time path prefixer + middleware scope. Groups must be
destroyed **before** their router is consumed by `aegon_server_create` (or simply
before router destruction; cleanup functions run at group destroy).

```c
AEGON_C_API aegon_group_t* aegon_router_group(aegon_router_t* router,
                                              const char* prefix) noexcept;          /* P1 */
AEGON_C_API aegon_group_t* aegon_group_group(aegon_group_t* parent,
                                             const char* sub_prefix) noexcept;       /* P1 */
AEGON_C_API void           aegon_group_destroy(aegon_group_t* group) noexcept;       /* P1 */

AEGON_C_API aegon_err_t aegon_group_add  (aegon_group_t*, aegon_method_t, const char* pattern,
                                          aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_group_get  (aegon_group_t*, const char*, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_group_post (aegon_group_t*, const char*, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_group_put  (aegon_group_t*, const char*, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_group_del  (aegon_group_t*, const char*, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_group_patch(aegon_group_t*, const char*, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
AEGON_C_API aegon_err_t aegon_group_all  (aegon_group_t*, const char*, aegon_handler_fn, void*, aegon_cleanup_fn) noexcept; /* P1 */
```

---

## 4.2 `include/aegon/c/server.h` — lifecycle, listeners, TLS, tuning — **Phase 1 (+3)**

C++ backing: `src/http/Server.h`, `src/http/ServerConfig.h`.

### Configuration aggregate (mirrors `ServerConfig`, `ProtocolLimits`, `TcpConfig`, `WebSocketConfig`)

```c
typedef struct aegon_server_config {
    size_t struct_size;              /* = sizeof(aegon_server_config_t) */

    /* --- I/O engine (EngineConfig + ServerConfig io_uring fields) --- */
    aegon_engine_t engine;           /* AUTO by default */
    uint32_t       ring_entries;     /* io_uring SQ depth (default 4096) */
    uint16_t       buffer_pool_entries; /* provided-buffer ring size (default 8192) */
    uint16_t       buffer_size;      /* per-buffer bytes (default 4096) */

    /* --- Protocol limits (ProtocolLimits) --- */
    size_t max_uri_length;           /* default 8192        -> 414 */
    size_t max_headers_size;         /* default 65536       -> 431 */
    size_t max_header_count;         /* default 100         -> 431 */
    size_t max_body_size;            /* default 16 MiB      -> 413 */

    /* --- TCP (TcpConfig) --- */
    bool tcp_nodelay;                /* default true */
    bool tcp_keepalive;              /* default true */
    int  tcp_keepidle;               /* seconds, default 30 */
    int  tcp_keepintvl;              /* seconds, default 10 */
    int  tcp_keepcnt;                /* probes,  default 3  */

    /* --- HTTP/2 & HTTP/3 (ServerConfig h2 fields) --- */
    uint32_t h2_max_concurrent_streams; /* default 256 */
    uint32_t h2_initial_window_size;    /* default 1 MiB */
    uint32_t rst_burst_limit;           /* rapid-reset flood threshold, default 1000 */

    /* --- WebSocket (WebSocketConfig) --- */
    size_t   ws_max_message_size;        /* default 16 MiB -> close 1009 */
    size_t   ws_max_frame_size;          /* default 16 MiB */
    bool     ws_require_masked_frames;   /* default true (RFC 6455 §5.1) */
    uint32_t ws_auto_ping_interval_sec;  /* 0 = disabled (default) */
    uint32_t ws_ping_timeout_sec;        /* default 10 */
    size_t   ws_initial_buffer_capacity; /* default 4096 */

    /* --- Feature toggles --- */
    bool enable_http3;               /* default true when TLS active */
} aegon_server_config_t;

/* Zeroes the struct, sets struct_size, applies all C++ defaults listed above. */
AEGON_C_API void aegon_server_config_init(aegon_server_config_t* cfg) noexcept;      /* P1 */
```

### Lifecycle

```c
/* Creates a server, CONSUMING the router (move semantics): on success *router is
 * set to NULL and must not be used or destroyed. `cfg` may be NULL (defaults);
 * otherwise it is fully copied — only fields covered by struct_size are read,
 * missing trailing fields keep defaults (01-architecture.md §A8). */
AEGON_C_API aegon_server_t* aegon_server_create(aegon_router_t** router,
                                                const aegon_server_config_t* cfg) noexcept; /* P1 */

/* Destroys the server. Must not be running (AEGON_ERR_STATE is impossible here —
 * the function is void; misuse is a contract violation documented loudly). */
AEGON_C_API void aegon_server_destroy(aegon_server_t* server) noexcept;              /* P1 */

/* Post-create config access for fluent-style tuning without a full config struct.
 * Each mirrors the corresponding Server setter. */
AEGON_C_API aegon_err_t aegon_server_set_config(aegon_server_t* server,
                                                const aegon_server_config_t* cfg) noexcept; /* P1 */

/* --- Listeners (Server::{listen,listen_tls}) — call BEFORE run. ---
 * host may be NULL ("0.0.0.0"). port must be 1..65535.
 * Multiple listeners accumulate, exactly like C++. */
AEGON_C_API aegon_err_t aegon_server_listen(aegon_server_t* server, uint16_t port,
                                            const char* host) noexcept;              /* P1 */
AEGON_C_API aegon_err_t aegon_server_listen_tls(aegon_server_t* server, uint16_t port,
                                                const char* host) noexcept;          /* P1 */

/* --- TLS (Server::enable_tls) — call BEFORE listen_tls/run. ---
 * PEM paths, copied. Empty strings -> Aegon's embedded self-signed dev certificate
 * (C++ default behavior). */
AEGON_C_API aegon_err_t aegon_server_enable_tls(aegon_server_t* server,
                                                const char* cert_file,
                                                const char* key_file) noexcept;      /* P1 */

/* --- Application data (C replacement for Server::provide<T>) ---
 * One pointer per server; readable via aegon_ctx_app_data in every handler on
 * every worker. Aegon never frees it. Call before run. */
AEGON_C_API aegon_err_t aegon_server_set_app_data(aegon_server_t* server,
                                                  void* app_data) noexcept;          /* P1 */
AEGON_C_API void*       aegon_server_app_data(aegon_server_t* server) noexcept;      /* P1 */
```

### Running

```c
/* Blocking run. `threads == 0` -> std::thread::hardware_concurrency().
 * Spawns the thread-per-core cluster (Server::run(size_t)); returns only after
 * aegon_server_stop() from another thread or SIGINT/SIGTERM handler calls stop.
 * AEGON_ERR_STATE if no listener was configured. */
AEGON_C_API aegon_err_t aegon_server_run(aegon_server_t* server, uint32_t threads) noexcept; /* P1 */

/* Non-blocking run for FFI hosts: spawns a supervisor thread that owns the
 * cluster and returns immediately. */
AEGON_C_API aegon_err_t aegon_server_run_async(aegon_server_t* server, uint32_t threads) noexcept; /* P1 */

/* Graceful stop. Thread-safe from ANY thread, including signal handlers on POSIX
 * (atomic store + engine wakeup; mirrors Server::stop()). Idempotent. */
AEGON_C_API void aegon_server_stop(aegon_server_t* server) noexcept;                 /* P1 */

/* Waits for run_async's supervisor thread to exit. No-op after blocking run.
 * Returns immediately if the server is not running asynchronously. */
AEGON_C_API aegon_err_t aegon_server_join(aegon_server_t* server) noexcept;          /* P1 */

AEGON_C_API bool aegon_server_is_running(aegon_server_t* server) noexcept;           /* P1 */
```

### Lifecycle hooks & background workers — **Phase 3** (need the async bridge)

```c
/* Server::{on_start,on_stop}. Hooks run on the supervisor context; for the C ABI
 * they are plain synchronous callbacks executed around cluster start/stop. */
typedef void (*aegon_lifecycle_fn)(aegon_server_t* server, void* userdata);

AEGON_C_API aegon_err_t aegon_server_on_start(aegon_server_t* server,
                                              aegon_lifecycle_fn fn,
                                              void* userdata,
                                              aegon_cleanup_fn cleanup) noexcept;    /* P3 */
AEGON_C_API aegon_err_t aegon_server_on_stop(aegon_server_t* server,
                                             aegon_lifecycle_fn fn,
                                             void* userdata,
                                             aegon_cleanup_fn cleanup) noexcept;     /* P3 */

/* Server::spawn_worker. The C worker is a long-running coroutine bridge: it runs
 * on worker loop 0 and must use aegon_async_* primitives to wait on external
 * events (never block the loop). Detailed contract in 06-api-streaming.md §async. */
typedef void (*aegon_worker_fn)(void* userdata);

AEGON_C_API aegon_err_t aegon_server_spawn_worker(aegon_server_t* server,
                                                  aegon_worker_fn fn,
                                                  void* userdata,
                                                  aegon_cleanup_fn cleanup) noexcept; /* P3 */
```

---

### Usage sketch (the Phase-1 acceptance example)

```c
#include <aegon/c/aegon.h>

static void hello(aegon_ctx_t* ctx, void* ud) {
    (void)ud;
    aegon_res_text(aegon_ctx_res(ctx), AEGON_STR("Hello from C\n"));
}

static void create_user(aegon_ctx_t* ctx, void* ud) {
    aegon_str_t body = aegon_req_body(aegon_ctx_req(ctx));
    /* parse body with your JSON lib of choice... */
    aegon_res_t* res = aegon_ctx_res(ctx);
    aegon_res_status(res, AEGON_STATUS_CREATED);
    aegon_res_json(res, body);   /* echo */
}

int main(void) {
    aegon_server_config_t cfg;
    aegon_server_config_init(&cfg);
    cfg.ring_entries = 8192;

    aegon_router_t* r = aegon_router_create();
    aegon_router_get(r, "/hello", hello, NULL, NULL);
    aegon_router_post(r, "/api/v1/users", create_user, NULL, NULL);

    aegon_server_t* s = aegon_server_create(&r, &cfg);   /* r == NULL afterwards */
    aegon_server_listen(s, 8080, "0.0.0.0");
    aegon_err_t err = aegon_server_run(s, 0);
    if (err != AEGON_OK) fprintf(stderr, "aegon: %s\n", aegon_last_error());
    aegon_server_destroy(s);
    return err == AEGON_OK ? 0 : 1;
}
```

---

### Cross-check against C++ sources

| C API | C++ backing |
| :--- | :--- |
| `aegon_router_create/destroy` | `Router` ctor/dtor (heap) |
| `aegon_router_add` + verb fns | `Router::add_route`, `RouteRegistrar::{get,post,put,del,patch,all}` |
| `aegon_router_on_error/not_found/method_not_allowed` | `Router::{set_error_handler,set_not_found_handler,set_method_not_allowed_handler}` |
| `aegon_router_group`, `aegon_group_*` | `Router::group`, `RouteGroup::{group,add_route,...}` |
| `aegon_server_config_t` | `ServerConfig` + `ProtocolLimits` + `TcpConfig` + `WebSocketConfig` + `EngineConfig` |
| `aegon_server_create` | `Server::Server(Router)` + setters |
| `aegon_server_listen/listen_tls/enable_tls` | `Server::{listen,listen_tls,enable_tls}` |
| `aegon_server_run/run_async/stop/join/is_running` | `Server::{run(size_t),stop}`, `running_` |
| `aegon_server_on_start/on_stop/spawn_worker` | `Server::{on_start,on_stop,spawn_worker}` |
| `aegon_server_set_app_data` | new (C substitute for `ServiceRegistry`) |
