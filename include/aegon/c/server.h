#ifndef AEGON_C_SERVER_H
#define AEGON_C_SERVER_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

typedef struct aegon_server aegon_server_t;  /* ≡ aegon::http::Server  (owned)     */

/* Configuration aggregate (mirrors ServerConfig, ProtocolLimits, TcpConfig, WebSocketConfig) */
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
AEGON_C_API void aegon_server_config_init(aegon_server_config_t* cfg);

/* Creates a server, CONSUMING the router (move semantics): on success *router is
 * set to NULL and must not be used or destroyed. `cfg` may be NULL (defaults);
 * otherwise it is fully copied — only fields covered by struct_size are read,
 * missing trailing fields keep defaults (01-architecture.md §A8). */
AEGON_C_API aegon_server_t* aegon_server_create(aegon_router_t** router,
                                                const aegon_server_config_t* cfg);

/* Destroys the server. Must not be running (AEGON_ERR_STATE is impossible here —
 * the function is void; misuse is a contract violation documented loudly). */
AEGON_C_API void aegon_server_destroy(aegon_server_t* server);

/* Post-create config access for fluent-style tuning without a full config struct.
 * Each mirrors the corresponding Server setter. */
AEGON_C_API aegon_err_t aegon_server_set_config(aegon_server_t* server,
                                                const aegon_server_config_t* cfg);

/* --- Listeners (Server::{listen,listen_tls}) — call BEFORE run. ---
 * host may be NULL ("0.0.0.0"). port must be 1..65535.
 * Multiple listeners accumulate, exactly like C++. */
AEGON_C_API aegon_err_t aegon_server_listen(aegon_server_t* server, uint16_t port,
                                            const char* host);
AEGON_C_API aegon_err_t aegon_server_listen_tls(aegon_server_t* server, uint16_t port,
                                                const char* host);

/* --- TLS (Server::enable_tls) — call BEFORE listen_tls/run. ---
 * PEM paths, copied. Empty strings -> Aegon's embedded self-signed dev certificate
 * (C++ default behavior). */
AEGON_C_API aegon_err_t aegon_server_enable_tls(aegon_server_t* server,
                                                const char* cert_file,
                                                const char* key_file);

/* --- Application data (C replacement for Server::provide<T>) ---
 * One pointer per server; readable via aegon_ctx_app_data in every handler on
 * every worker. Aegon never frees it. Call before run. */
AEGON_C_API aegon_err_t aegon_server_set_app_data(aegon_server_t* server,
                                                  void* app_data);
AEGON_C_API void*       aegon_server_app_data(aegon_server_t* server);

/* --- Running --- */
AEGON_C_API aegon_err_t aegon_server_run(aegon_server_t* server, uint32_t threads);
AEGON_C_API aegon_err_t aegon_server_run_async(aegon_server_t* server, uint32_t threads);
AEGON_C_API void        aegon_server_stop(aegon_server_t* server);
AEGON_C_API aegon_err_t aegon_server_join(aegon_server_t* server);
AEGON_C_API bool        aegon_server_is_running(aegon_server_t* server);

AEGON_C_END

#endif /* AEGON_C_SERVER_H */

