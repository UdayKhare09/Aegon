# 05 — Middleware API: `middleware.h`, `cors.h`, `compress.h`, `security.h`, `access_log.h`, `auth.h`, `static_files.h`

**All Phase 2.** C++ backing: `src/http/Middleware.h`, `src/http/middleware/*`,
`Router::static_files`, `src/http/RouteGroup.h` (`StaticFilesOptions`).

> ⚠ **Fast-path interaction** (`01-architecture.md` §A7): registering **any** global
> middleware (`aegon_router_use*`) forces Aegon's asynchronous dispatch path for the
> whole router, exactly like C++. Group- and route-level middleware only affect their
> subtree. Built-in middleware registered through these headers has identical cost to
> its C++ counterpart (they wrap the same factory functions).

---

## 5.1 `include/aegon/c/middleware.h` — custom C middleware

Aegon middleware is onion-style: code before `next` runs on the way in, code after
`next` runs on the way out; not calling `next` short-circuits.

```c
#ifndef AEGON_C_MIDDLEWARE_H
#define AEGON_C_MIDDLEWARE_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef struct aegon_next aegon_next_t;   /* opaque chain-continuation token */

/* Middleware callback. MUST either:
 *   - call aegon_next_call(next, ctx) exactly once (continue the chain), or
 *   - not call it (short-circuit: response set on ctx is sent as-is).
 * `next` is valid ONLY during this invocation (stack-allocated chain state). */
typedef void (*aegon_middleware_fn)(aegon_ctx_t* ctx, aegon_next_t* next, void* userdata);

/* Continue the onion chain. Safe to call exactly once. After it returns, the
 * response is finalized for this middleware's post-processing. */
AEGON_C_API void aegon_next_call(aegon_next_t* next, aegon_ctx_t* ctx) noexcept;     /* P2 */

/* Global middleware (Router::use). */
AEGON_C_API aegon_err_t aegon_router_use(aegon_router_t* router,
                                         aegon_middleware_fn mw,
                                         void* userdata,
                                         aegon_cleanup_fn cleanup) noexcept;         /* P2 */

/* Group middleware (RouteGroup::use) — runs for every route in the subtree. */
AEGON_C_API aegon_err_t aegon_group_use(aegon_group_t* group,
                                        aegon_middleware_fn mw,
                                        void* userdata,
                                        aegon_cleanup_fn cleanup) noexcept;          /* P2 */

/* Per-route middleware (add_route route_mw vector). Registration-time only;
 * stack arrays are fine (contents are copied). */
typedef struct aegon_middleware_slot {
    aegon_middleware_fn fn;
    void*               userdata;
    aegon_cleanup_fn    cleanup;
} aegon_middleware_slot_t;

AEGON_C_API aegon_err_t aegon_router_add_mw(aegon_router_t* router,
                                            aegon_method_t method,
                                            const char* pattern,
                                            const aegon_middleware_slot_t* mws,
                                            size_t mw_count,
                                            aegon_handler_fn handler,
                                            void* userdata,
                                            aegon_cleanup_fn cleanup) noexcept;      /* P2 */
AEGON_C_API aegon_err_t aegon_group_add_mw(aegon_group_t* group,
                                           aegon_method_t method,
                                           const char* pattern,
                                           const aegon_middleware_slot_t* mws,
                                           size_t mw_count,
                                           aegon_handler_fn handler,
                                           void* userdata,
                                           aegon_cleanup_fn cleanup) noexcept;       /* P2 */

AEGON_C_END

#endif /* AEGON_C_MIDDLEWARE_H */
```

Implementation note: the C wrapper builds a C++ `MiddlewareFn` per slot that
invokes `aegon_middleware_fn` with a stack-resident `aegon_next_t` holder wrapping
the C++ `Next` callable. `aegon_next_call` invokes it. The C++ chain runner
(`run_chain`) is reused unchanged.

---

## 5.2 `include/aegon/c/cors.h` — CORS middleware

Mirrors `middleware::CorsConfig` + `middleware::cors()`.

```c
#ifndef AEGON_C_CORS_H
#define AEGON_C_CORS_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef struct aegon_cors_config {
    size_t struct_size;

    /* Exact origins, e.g. {"https://app.example.com"} or {"*"} for permissive.
     * Array of NUL-terminated strings, copied at registration. NULL/0 -> none. */
    const char* const* allowed_origins;
    size_t             allowed_origins_count;

    /* Wildcard patterns, e.g. {"https://*.example.com", "http://localhost:*"}. */
    const char* const* origin_patterns;
    size_t             origin_patterns_count;

    /* Allowed preflight methods (default AEGON_METHODS_STANDARD). */
    aegon_method_mask_t allowed_methods;

    /* Allowed request headers; {"*"} reflects request headers. NULL/0 -> {"*"}. */
    const char* const* allowed_headers;
    size_t             allowed_headers_count;

    /* Exposed response headers. NULL/0 -> none. */
    const char* const* expose_headers;
    size_t             expose_headers_count;

    bool     allow_credentials;       /* default false */
    bool     allow_private_network;   /* W3C PNA, default false */
    uint32_t max_age_seconds;         /* default 86400 */
    bool     vary_header;             /* default true */
} aegon_cors_config_t;

/* Applies C++ CorsConfig defaults (permissive-ish skeleton: origins empty,
 * methods STANDARD, headers {"*"}, credentials false, max_age 86400, vary true). */
AEGON_C_API void aegon_cors_config_init(aegon_cors_config_t* cfg) noexcept;          /* P2 */

AEGON_C_API aegon_err_t aegon_router_use_cors(aegon_router_t* router,
                                              const aegon_cors_config_t* cfg) noexcept; /* P2 */
AEGON_C_API aegon_err_t aegon_group_use_cors(aegon_group_t* group,
                                             const aegon_cors_config_t* cfg) noexcept;  /* P2 */

AEGON_C_END

#endif /* AEGON_C_CORS_H */
```

The C++ `origin_validator` lambda slot is intentionally **not** exposed in v1 —
expressible today via a custom `aegon_middleware_fn` instead. (Candidate for a later
minor: `aegon_cors_config_t.origin_validator` C callback; appended trailing field.)

---

## 5.3 `include/aegon/c/compress.h` — gzip/deflate compression

Mirrors `middleware::CompressOptions` + `middleware::Compress()`.

```c
#ifndef AEGON_C_COMPRESS_H
#define AEGON_C_COMPRESS_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef struct aegon_compress_config {
    size_t struct_size;
    int    level;           /* 1 (fastest, default) .. 12 (max) — libdeflate */
    size_t min_size;        /* skip bodies smaller than this; default 256 */
    bool   gzip;            /* default true */
    bool   deflate;         /* default true */
    bool   prefer_deflate;  /* default true */
    bool   vary;            /* merge Vary: Accept-Encoding; default true */
} aegon_compress_config_t;

AEGON_C_API void aegon_compress_config_init(aegon_compress_config_t* cfg) noexcept;  /* P2 */

AEGON_C_API aegon_err_t aegon_router_use_compress(aegon_router_t* router,
                                                  const aegon_compress_config_t* cfg) noexcept; /* P2 */
AEGON_C_API aegon_err_t aegon_group_use_compress(aegon_group_t* group,
                                                 const aegon_compress_config_t* cfg) noexcept;  /* P2 */

AEGON_C_END

#endif /* AEGON_C_COMPRESS_H */
```

---

## 5.4 `include/aegon/c/security.h` — security headers

Mirrors `middleware::{FrameOption, ReferrerPolicy, CrossOriginOpenerPolicy,
CrossOriginEmbedderPolicy, CrossOriginResourcePolicy, HstsConfig, SecurityHeadersConfig}`
+ `middleware::security_headers()`.

```c
#ifndef AEGON_C_SECURITY_H
#define AEGON_C_SECURITY_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef enum aegon_frame_option {
    AEGON_FRAME_DENY       = 0,
    AEGON_FRAME_SAMEORIGIN = 1,
    AEGON_FRAME_DISABLED   = 2
} aegon_frame_option_t;

typedef enum aegon_referrer_policy {
    AEGON_REFERRER_NO_REFERRER                    = 0,
    AEGON_REFERRER_NO_REFERRER_WHEN_DOWNGRADE     = 1,
    AEGON_REFERRER_SAME_ORIGIN                    = 2,
    AEGON_REFERRER_ORIGIN                         = 3,
    AEGON_REFERRER_STRICT_ORIGIN                  = 4,
    AEGON_REFERRER_ORIGIN_WHEN_CROSS_ORIGIN       = 5,
    AEGON_REFERRER_STRICT_ORIGIN_WHEN_CROSS_ORIGIN = 6,  /* default */
    AEGON_REFERRER_UNSAFE_URL                     = 7
} aegon_referrer_policy_t;

typedef enum aegon_coop {
    AEGON_COOP_SAME_ORIGIN = 0,   /* default */
    AEGON_COOP_SAME_ORIGIN_ALLOW_POPUPS = 1,
    AEGON_COOP_UNSAFE_NONE = 2,
    AEGON_COOP_DISABLED    = 3
} aegon_coop_t;

typedef enum aegon_coep {
    AEGON_COEP_REQUIRE_CORP = 0,
    AEGON_COEP_CREDENTIALLESS = 1,
    AEGON_COEP_UNSAFE_NONE  = 2,
    AEGON_COEP_DISABLED     = 3   /* default */
} aegon_coep_t;

typedef enum aegon_corp {
    AEGON_CORP_SAME_ORIGIN  = 0,  /* default */
    AEGON_CORP_SAME_SITE    = 1,
    AEGON_CORP_CROSS_ORIGIN = 2,
    AEGON_CORP_DISABLED     = 3
} aegon_corp_t;

typedef struct aegon_security_config {
    size_t struct_size;
    bool     content_type_nosniff;        /* default true  */
    aegon_frame_option_t frame_options;   /* default DENY  */
    bool     xss_protection_disabled;     /* default true (X-XSS-Protection: 0) */
    aegon_referrer_policy_t referrer_policy;

    /* HSTS */
    bool     hsts_enabled;                /* default true */
    uint32_t hsts_max_age_seconds;        /* default 31536000 */
    bool     hsts_include_subdomains;     /* default true */
    bool     hsts_preload;                /* default false */

    /* Raw policy strings (NUL-terminated, copied; NULL/empty -> header omitted) */
    const char* content_security_policy;  /* default NULL */
    bool        csp_report_only;          /* default false */
    const char* permissions_policy;       /* default NULL */

    aegon_coop_t coop;
    aegon_coep_t coep;
    aegon_corp_t corp;

    bool prevent_browser_caching;         /* default false */
} aegon_security_config_t;

/* Applies SecurityHeadersConfig::defaults(). */
AEGON_C_API void aegon_security_config_init(aegon_security_config_t* cfg) noexcept;  /* P2 */

AEGON_C_API aegon_err_t aegon_router_use_security_headers(aegon_router_t* router,
                                                          const aegon_security_config_t* cfg) noexcept; /* P2 */
AEGON_C_API aegon_err_t aegon_group_use_security_headers(aegon_group_t* group,
                                                         const aegon_security_config_t* cfg) noexcept;  /* P2 */

AEGON_C_END

#endif /* AEGON_C_SECURITY_H */
```

---

## 5.5 `include/aegon/c/access_log.h` — access logging

Mirrors `middleware::{AccessLogFormat, AccessLogConfig}` + `middleware::access_log()`.
The C++ `custom_formatter` / `skip_if` `std::function` slots are exposed as C
callbacks (both are hot-path-callable; document thread confinement).

```c
#ifndef AEGON_C_ACCESS_LOG_H
#define AEGON_C_ACCESS_LOG_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef enum aegon_access_log_format {
    AEGON_ACCESS_LOG_COMBINED = 0,   /* default */
    AEGON_ACCESS_LOG_COMMON   = 1,
    AEGON_ACCESS_LOG_JSON     = 2,
    AEGON_ACCESS_LOG_CUSTOM   = 3
} aegon_access_log_format_t;

/* Return true to SKIP logging this request. Thread-confined (see §A6). */
typedef bool (*aegon_access_log_skip_fn)(aegon_ctx_t* ctx, void* userdata);

/* Custom formatter: write the finished line into buf (NUL-terminate), return the
 * byte count written (excluding NUL), or 0 to skip. If the line doesn't fit,
 * return 0 — Aegon logs a truncation marker at WARN. */
typedef size_t (*aegon_access_log_format_fn)(aegon_ctx_t* ctx,
                                             uint64_t duration_ns,
                                             char* buf, size_t buf_cap,
                                             void* userdata);

typedef struct aegon_access_log_config {
    size_t struct_size;
    aegon_access_log_format_t format;

    aegon_log_level_t normal_level;         /* default INFO */
    aegon_log_level_t client_error_level;   /* default WARN  (4xx) */
    aegon_log_level_t server_error_level;   /* default ERROR (5xx) */

    uint64_t          slow_threshold_ns;    /* 0 = disabled (default) */
    aegon_log_level_t slow_level;           /* default WARN */

    bool log_query;        /* default true */
    bool log_client_ip;    /* default true (X-Forwarded-For / X-Real-IP) */
    bool log_bytes;        /* default true */
    bool log_protocol;     /* default true */

    /* Header names to append; arrays of NUL-terminated strings, copied. */
    const char* const* request_headers;  size_t request_headers_count;   /* NULL/0 */
    const char* const* response_headers; size_t response_headers_count;  /* NULL/0 */

    /* Exact paths to skip (e.g. {"/health"}). */
    const char* const* skip_paths;       size_t skip_paths_count;        /* NULL/0 */

    aegon_access_log_skip_fn    skip_if;         /* NULL */
    void*                       skip_userdata;
    aegon_access_log_format_fn  custom_formatter;/* NULL; used when format==CUSTOM */
    void*                       formatter_userdata;

    const char* correlation_header;       /* default "x-request-id"; NULL/"" disables */
} aegon_access_log_config_t;

AEGON_C_API void aegon_access_log_config_init(aegon_access_log_config_t* cfg) noexcept; /* P2 */

AEGON_C_API aegon_err_t aegon_router_use_access_log(aegon_router_t* router,
                                                    const aegon_access_log_config_t* cfg) noexcept; /* P2 */
AEGON_C_API aegon_err_t aegon_group_use_access_log(aegon_group_t* group,
                                                   const aegon_access_log_config_t* cfg) noexcept;  /* P2 */

AEGON_C_END

#endif /* AEGON_C_ACCESS_LOG_H */
```

---

## 5.6 `include/aegon/c/auth.h` — authentication middleware

Mirrors `middleware::{bearer_auth, basic_auth, api_key_auth, cookie_auth}`.
The typed C++ principal (`ctx.set<T>`) is replaced by a user-owned void* plus
`aegon_ctx_local_set` — see `01-architecture.md` §A10.

```c
#ifndef AEGON_C_AUTH_H
#define AEGON_C_AUTH_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* --- Validators (thread-confined, synchronous) ---
 * Return true = authenticated; optionally attach a principal for downstream
 * handlers via `aegon_ctx_local_set(ctx, "auth.principal", ptr)` inside the
 * validator (convention key, not enforced). Return false -> 401 problem+json. */

typedef bool (*aegon_auth_token_fn)(aegon_ctx_t* ctx, aegon_str_t token, void* userdata);
typedef bool (*aegon_auth_basic_fn)(aegon_ctx_t* ctx, aegon_str_t username,
                                    aegon_str_t password, void* userdata);

/* --- Bearer: Authorization: Bearer <token> --- */
AEGON_C_API aegon_err_t aegon_router_use_bearer_auth(aegon_router_t* router,
                                                     aegon_auth_token_fn validator,
                                                     void* userdata,
                                                     aegon_cleanup_fn cleanup) noexcept; /* P2 */
AEGON_C_API aegon_err_t aegon_group_use_bearer_auth(aegon_group_t* group,
                                                    aegon_auth_token_fn validator,
                                                    void* userdata,
                                                    aegon_cleanup_fn cleanup) noexcept;  /* P2 */

/* --- Basic (RFC 7617). `realm` copied; NULL -> "Access". --- */
AEGON_C_API aegon_err_t aegon_router_use_basic_auth(aegon_router_t* router,
                                                    const char* realm,
                                                    aegon_auth_basic_fn validator,
                                                    void* userdata,
                                                    aegon_cleanup_fn cleanup) noexcept;  /* P2 */
AEGON_C_API aegon_err_t aegon_group_use_basic_auth(aegon_group_t* group,
                                                   const char* realm,
                                                   aegon_auth_basic_fn validator,
                                                   void* userdata,
                                                   aegon_cleanup_fn cleanup) noexcept;   /* P2 */

/* --- API key: reads header `header_name` (e.g. "x-api-key"), copied. --- */
AEGON_C_API aegon_err_t aegon_router_use_api_key_auth(aegon_router_t* router,
                                                      const char* header_name,
                                                      aegon_auth_token_fn validator,
                                                      void* userdata,
                                                      aegon_cleanup_fn cleanup) noexcept; /* P2 */
AEGON_C_API aegon_err_t aegon_group_use_api_key_auth(aegon_group_t* group,
                                                     const char* header_name,
                                                     aegon_auth_token_fn validator,
                                                     void* userdata,
                                                     aegon_cleanup_fn cleanup) noexcept;  /* P2 */

/* --- Cookie/session: reads cookie `cookie_name` (Request::cookie), copied. --- */
AEGON_C_API aegon_err_t aegon_router_use_cookie_auth(aegon_router_t* router,
                                                     const char* cookie_name,
                                                     aegon_auth_token_fn validator,
                                                     void* userdata,
                                                     aegon_cleanup_fn cleanup) noexcept; /* P2 */
AEGON_C_API aegon_err_t aegon_group_use_cookie_auth(aegon_group_t* group,
                                                    const char* cookie_name,
                                                    aegon_auth_token_fn validator,
                                                    void* userdata,
                                                    aegon_cleanup_fn cleanup) noexcept;  /* P2 */

AEGON_C_END

#endif /* AEGON_C_AUTH_H */
```

Notes:
- C++ `abort_on_failure` is **not** exposed; the C ABI always aborts (sends 401 and
  short-circuits). The permissive variant is trivially expressible as custom
  middleware. Trailing config structs are a possible later addition.
- C++ `on_unauthorized` customization is likewise omitted in v1 (default
  `problem+json` 401 with `WWW-Authenticate`, matching C++ defaults).

---

## 5.7 `include/aegon/c/static_files.h` — static file serving

Mirrors `StaticFilesOptions` + `Router::static_files` / `RouteGroup::static_files`.

```c
#ifndef AEGON_C_STATIC_FILES_H
#define AEGON_C_STATIC_FILES_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* Optional Cache-Control policy callback (StaticFilesOptions::cache_control).
 * `path` is the URL path requested; `is_fallback` is true when serving the SPA
 * fallback document. Write into buf (NUL-terminate), return byte count; return 0
 * to omit the header. Thread-confined. */
typedef size_t (*aegon_cache_control_fn)(aegon_str_t path, bool is_fallback,
                                         char* buf, size_t buf_cap, void* userdata);

typedef struct aegon_static_files_config {
    size_t struct_size;
    bool precompressed;        /* serve .br/.gz sidecars; default true */
    bool cache_in_memory;      /* mtime-revalidated in-memory cache; default true */
    const char* index_file;    /* default "index.html"; copied */
    bool spa;                  /* HTML5 history fallback; default false */
    const char* fallback_file; /* default "index.html"; copied */
    bool etag;                 /* default true */
    bool last_modified;        /* default true */
    aegon_cache_control_fn cache_control;   /* NULL -> framework defaults */
    void*                  cache_control_userdata;
} aegon_static_files_config_t;

AEGON_C_API void aegon_static_files_config_init(aegon_static_files_config_t* cfg) noexcept; /* P2 */

/* Serves `directory` under URL `prefix`. AEGON_ERR_INVALID_ARG if either path is
 * empty; AEGON_ERR_IO if the directory doesn't exist at registration time. */
AEGON_C_API aegon_err_t aegon_router_static_files(aegon_router_t* router,
                                                  const char* prefix,
                                                  const char* directory,
                                                  const aegon_static_files_config_t* cfg) noexcept; /* P2 */
AEGON_C_API aegon_err_t aegon_group_static_files(aegon_group_t* group,
                                                 const char* prefix,
                                                 const char* directory,
                                                 const aegon_static_files_config_t* cfg) noexcept;  /* P2 */

AEGON_C_END

#endif /* AEGON_C_STATIC_FILES_H */
```

---

### Cross-check against C++ sources

| C API | C++ backing |
| :--- | :--- |
| `aegon_middleware_fn`, `aegon_next_call` | `MiddlewareFn`, `Next`, `run_chain`, `make_middleware` |
| `aegon_router_use`, `aegon_group_use` | `Router::use`, `RouteGroup::use` |
| `aegon_router_add_mw`, `aegon_group_add_mw` | `add_route(..., group_mw, route_mw, ...)` |
| `aegon_cors_config_t`, `*_use_cors` | `middleware::{CorsConfig,cors}` |
| `aegon_compress_config_t`, `*_use_compress` | `middleware::{CompressOptions,Compress}` |
| `aegon_security_config_t`, `*_use_security_headers` | `middleware::{SecurityHeadersConfig,security_headers}` + enums |
| `aegon_access_log_config_t`, `*_use_access_log` | `middleware::{AccessLogConfig,access_log}` |
| `*_use_bearer/basic/api_key/cookie_auth` | `middleware::{bearer_auth,basic_auth,api_key_auth,cookie_auth}` |
| `aegon_static_files_config_t`, `*_static_files` | `StaticFilesOptions`, `Router::static_files`, `RouteGroup::static_files` |
