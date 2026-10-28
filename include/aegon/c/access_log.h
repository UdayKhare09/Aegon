#ifndef AEGON_C_ACCESS_LOG_H
#define AEGON_C_ACCESS_LOG_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/context.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

typedef enum aegon_access_log_format {
    AEGON_ACCESS_LOG_COMBINED = 0,   /* default */
    AEGON_ACCESS_LOG_COMMON   = 1,
    AEGON_ACCESS_LOG_JSON     = 2,
    AEGON_ACCESS_LOG_CUSTOM   = 3
} aegon_access_log_format_t;

/* Return true to SKIP logging this request. */
typedef bool (*aegon_access_log_skip_fn)(aegon_ctx_t* ctx, void* userdata);

/* Custom formatter: write the finished line into buf (NUL-terminate), return the
 * byte count written (excluding NUL), or 0 to skip. */
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
    const char* const* request_headers;
    size_t             request_headers_count;
    const char* const* response_headers;
    size_t             response_headers_count;

    /* Exact paths to skip (e.g. {"/health"}). */
    const char* const* skip_paths;
    size_t             skip_paths_count;

    aegon_access_log_skip_fn    skip_if;
    void*                       skip_userdata;
    aegon_access_log_format_fn  custom_formatter; /* used when format==CUSTOM */
    void*                       formatter_userdata;

    const char* correlation_header;       /* default "x-request-id"; NULL/"" disables */
} aegon_access_log_config_t;

AEGON_C_API void aegon_access_log_config_init(aegon_access_log_config_t* cfg);

AEGON_C_API aegon_err_t aegon_router_use_access_log(aegon_router_t* router,
                                                    const aegon_access_log_config_t* cfg);
AEGON_C_API aegon_err_t aegon_group_use_access_log(aegon_group_t* group,
                                                   const aegon_access_log_config_t* cfg);

AEGON_C_END

#endif /* AEGON_C_ACCESS_LOG_H */

