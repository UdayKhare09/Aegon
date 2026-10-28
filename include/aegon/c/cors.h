#ifndef AEGON_C_CORS_H
#define AEGON_C_CORS_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

typedef struct aegon_cors_config {
    size_t struct_size;

    /* Exact origins, e.g. {"https://app.example.com"} or {"*"} for permissive.
     * Array of NUL-terminated strings, copied at registration. NULL/0 -> none. */
    const char* const* allowed_origins;
    size_t             allowed_origins_count;

    /* Wildcard patterns, e.g. {"https://sub.example.com" with globs}. */
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
AEGON_C_API void aegon_cors_config_init(aegon_cors_config_t* cfg);

AEGON_C_API aegon_err_t aegon_router_use_cors(aegon_router_t* router,
                                              const aegon_cors_config_t* cfg);
AEGON_C_API aegon_err_t aegon_group_use_cors(aegon_group_t* group,
                                             const aegon_cors_config_t* cfg);

AEGON_C_END

#endif /* AEGON_C_CORS_H */
