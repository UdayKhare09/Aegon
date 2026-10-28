#ifndef AEGON_C_TYPES_H
#define AEGON_C_TYPES_H

#include <aegon/c/export.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

AEGON_C_BEGIN

/* -------------------------------------------------------------------------
 * Version (compile-time, mirrors src/Version.h)
 * ------------------------------------------------------------------------- */
#ifndef AEGON_VERSION_MAJOR
#define AEGON_VERSION_MAJOR 0
#define AEGON_VERSION_MINOR 2
#define AEGON_VERSION_PATCH 1
#define AEGON_VERSION_STRING "0.2.1"
#endif

#ifndef AEGON_VERSION_HEX
#define AEGON_VERSION_HEX ((AEGON_VERSION_MAJOR << 16) | (AEGON_VERSION_MINOR << 8) | AEGON_VERSION_PATCH)
#endif

/* ABI epoch: bumped only on breaking ABI change (drives SONAME). */
#define AEGON_ABI_VERSION 1

/* -------------------------------------------------------------------------
 * String slice — see 01-architecture.md §A1
 * ------------------------------------------------------------------------- */
typedef struct aegon_str {
    const char* data;   /* NOT necessarily NUL-terminated; may be NULL iff len == 0 */
    size_t      len;
} aegon_str_t;

/* Compile-time slice from a string literal: aegon_res_text(res, AEGON_STR("OK")); */
#define AEGON_STR(lit) ((aegon_str_t){ (lit), sizeof(lit) - 1u })

/* Runtime wrap of a NUL-terminated C string (strlen applied; NULL -> {NULL,0}). */
AEGON_C_API aegon_str_t aegon_str(const char* cstr);

/* Heap-copy a slice into an Aegon-allocated, NUL-terminated buffer.
 * Free with aegon_mem_free(). Returns {NULL,0} on OOM. */
AEGON_C_API aegon_str_t aegon_str_dup(aegon_str_t s);

/* Frees memory returned by aegon_str_dup (and any future aegon_*_dup).
 * NULL-safe. Always use this instead of free() — CRT pairing on Windows. */
AEGON_C_API void aegon_mem_free(void* p);

/* -------------------------------------------------------------------------
 * Error codes — append-only. aegon_err_str() covers every value.
 * ------------------------------------------------------------------------- */
typedef enum aegon_err {
    AEGON_OK                   = 0,
    AEGON_ERR_INVALID_ARG      = 1,   /* NULL handle, port == 0, bad pattern, bad config */
    AEGON_ERR_NOMEM            = 2,   /* allocation failure */
    AEGON_ERR_IO               = 3,   /* socket / io_uring / file I/O failure */
    AEGON_ERR_TLS              = 4,   /* OpenSSL context, certificate, or handshake setup failure */
    AEGON_ERR_NOT_FOUND        = 5,   /* requested key/header/param absent (when not optional-returning) */
    AEGON_ERR_STATE            = 6,   /* illegal call order (e.g. listen() after run(), destroying running server) */
    AEGON_ERR_UNSUPPORTED      = 7,   /* feature unavailable on this platform (e.g. io_uring on Windows) */
    AEGON_ERR_BUFFER_TOO_SMALL = 8,   /* caller-provided output buffer insufficient */
    AEGON_ERR_INTERNAL         = 9    /* uncaught C++ exception reached the firewall; see aegon_last_error() */
} aegon_err_t;

/* -------------------------------------------------------------------------
 * Logging severity — mirrors aegon::log::Level
 * ------------------------------------------------------------------------- */
typedef enum aegon_log_level {
    AEGON_LOG_TRACE = 0,
    AEGON_LOG_DEBUG = 1,
    AEGON_LOG_INFO  = 2,
    AEGON_LOG_WARN  = 3,
    AEGON_LOG_ERROR = 4,
    AEGON_LOG_FATAL = 5,
    AEGON_LOG_OFF   = 6
} aegon_log_level_t;

/* -------------------------------------------------------------------------
 * HTTP method — mirrors aegon::http::Method (aegon_method_cstr round-trips
 * with aegon_method_from_str).
 * ------------------------------------------------------------------------- */
typedef enum aegon_method {
    AEGON_METHOD_GET     = 0,
    AEGON_METHOD_POST    = 1,
    AEGON_METHOD_PUT     = 2,
    AEGON_METHOD_DELETE  = 3,
    AEGON_METHOD_PATCH   = 4,
    AEGON_METHOD_HEAD    = 5,
    AEGON_METHOD_OPTIONS = 6,
    AEGON_METHOD_CONNECT = 7,
    AEGON_METHOD_TRACE   = 8,
    AEGON_METHOD_UNKNOWN = 9
} aegon_method_t;

/* Bit-set helpers for aegon_method_mask_t (used by CORS allowed_methods). */
typedef uint16_t aegon_method_mask_t;
#define AEGON_METHOD_BIT(m) ((aegon_method_mask_t)(1u << (unsigned)(m)))
#define AEGON_METHODS_ALL   ((aegon_method_mask_t)0x1FFu)
#define AEGON_METHODS_STANDARD (AEGON_METHOD_BIT(AEGON_METHOD_GET)    | \
                                AEGON_METHOD_BIT(AEGON_METHOD_POST)   | \
                                AEGON_METHOD_BIT(AEGON_METHOD_PUT)    | \
                                AEGON_METHOD_BIT(AEGON_METHOD_DELETE) | \
                                AEGON_METHOD_BIT(AEGON_METHOD_PATCH)  | \
                                AEGON_METHOD_BIT(AEGON_METHOD_HEAD)   | \
                                AEGON_METHOD_BIT(AEGON_METHOD_OPTIONS))

/* -------------------------------------------------------------------------
 * HTTP version — mirrors aegon::http::HttpVersion
 * ------------------------------------------------------------------------- */
typedef enum aegon_http_version {
    AEGON_HTTP_1_0 = 0,
    AEGON_HTTP_1_1 = 1,
    AEGON_HTTP_2   = 2,
    AEGON_HTTP_3   = 3
} aegon_http_version_t;

/* -------------------------------------------------------------------------
 * Cookie SameSite — mirrors aegon::http::SameSite
 * ------------------------------------------------------------------------- */
typedef enum aegon_samesite {
    AEGON_SAMESITE_DEFAULT = 0,
    AEGON_SAMESITE_LAX     = 1,
    AEGON_SAMESITE_STRICT  = 2,
    AEGON_SAMESITE_NONE    = 3
} aegon_samesite_t;

/* -------------------------------------------------------------------------
 * I/O engine preference — mirrors aegon::core::EngineType
 * ------------------------------------------------------------------------- */
typedef enum aegon_engine {
    AEGON_ENGINE_AUTO    = 0,   /* probe best available (io_uring -> epoll / IOCP) */
    AEGON_ENGINE_IOURING = 1,   /* Linux only; AEGON_ERR_UNSUPPORTED elsewhere */
    AEGON_ENGINE_EPOLL   = 2,   /* Linux fallback */
    AEGON_ENGINE_KQUEUE  = 3,   /* reserved (not yet implemented in Aegon) */
    AEGON_ENGINE_IOCP    = 4    /* Windows */
} aegon_engine_t;

AEGON_C_END

#endif /* AEGON_C_TYPES_H */
