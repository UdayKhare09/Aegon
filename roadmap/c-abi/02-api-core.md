# 02 — Core API: `export.h`, `aegon.h`, `types.h`, `error.h`, `log.h`

Every signature below is exactly what will appear in the public headers.
`// P0/P1` margin tags = implementation phase (see `08-phases.md`).

---

## 2.1 `include/aegon/c/export.h` — visibility & extern guards — **Phase 0**

```c
#ifndef AEGON_C_EXPORT_H
#define AEGON_C_EXPORT_H

/* Symbol visibility. Define AEGON_C_STATIC when linking libaegon_c statically. */
#if defined(AEGON_C_STATIC)
#  define AEGON_C_API
#elif defined(_WIN32)
#  if defined(AEGON_C_BUILDING)           /* set only while compiling libaegon_c itself */
#    define AEGON_C_API __declspec(dllexport)
#  else
#    define AEGON_C_API __declspec(dllimport)
#  endif
#else
#  define AEGON_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
#  define AEGON_C_BEGIN extern "C" {
#  define AEGON_C_END   }
#else
#  define AEGON_C_BEGIN
#  define AEGON_C_END
#endif

#endif /* AEGON_C_EXPORT_H */
```

Implementation notes:
- `AEGON_C_BUILDING` is set via `target_compile_definitions(aegon_c PRIVATE AEGON_C_BUILDING)`.
- Linux shared build additionally uses a version script (`cmake/aegon_c.map`) exporting
  only `aegon_*` symbols — see `07-build-testing.md`.

---

## 2.2 `include/aegon/c/types.h` — strings, errors, enums, memory — **Phase 0 (errors/enums) + Phase 1**

```c
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
#define AEGON_VERSION_MAJOR 0
#define AEGON_VERSION_MINOR 2
#define AEGON_VERSION_PATCH 1
#define AEGON_VERSION_STRING "0.2.1"
#define AEGON_VERSION_HEX ((AEGON_VERSION_MAJOR << 16) | (AEGON_VERSION_MINOR << 8) | AEGON_VERSION_PATCH)

/* ABI epoch: bumped only on breaking ABI change (drives SONAME). */
#define AEGON_ABI_VERSION 1

/* -------------------------------------------------------------------------
 * String slice — see 01-architecture.md §A1
 * ------------------------------------------------------------------------- */
typedef struct aegon_str {
    const char* data;
    size_t      len;
} aegon_str_t;

/* Compile-time slice from a string literal: aegon_res_text(res, AEGON_STR("OK")); */
#define AEGON_STR(lit) ((aegon_str_t){ (lit), sizeof(lit) - 1u })

/* Runtime wrap of a NUL-terminated C string (strlen applied; NULL -> {NULL,0}). */
AEGON_C_API aegon_str_t aegon_str(const char* cstr) noexcept;             /* P1 */

/* Heap-copy a slice into an Aegon-allocated, NUL-terminated buffer.
 * Free with aegon_mem_free(). Returns {NULL,0} on OOM. */
AEGON_C_API aegon_str_t aegon_str_dup(aegon_str_t s) noexcept;            /* P1 */

/* Frees memory returned by aegon_str_dup (and any future aegon_*_dup).
 * NULL-safe. Always use this instead of free() — CRT pairing on Windows. */
AEGON_C_API void aegon_mem_free(void* p) noexcept;                        /* P1 */

/* -------------------------------------------------------------------------
 * Error codes — append-only. aegon_err_str() covers every value.
 * ------------------------------------------------------------------------- */
typedef enum aegon_err {
    AEGON_OK                = 0,
    AEGON_ERR_INVALID_ARG   = 1,   /* NULL handle, port == 0, bad pattern, bad config */
    AEGON_ERR_NOMEM         = 2,   /* allocation failure */
    AEGON_ERR_IO            = 3,   /* socket / io_uring / file I/O failure */
    AEGON_ERR_TLS           = 4,   /* OpenSSL context, certificate, or handshake setup failure */
    AEGON_ERR_NOT_FOUND     = 5,   /* requested key/header/param absent (when not optional-returning) */
    AEGON_ERR_STATE         = 6,   /* illegal call order (e.g. listen() after run(), destroying running server) */
    AEGON_ERR_UNSUPPORTED   = 7,   /* feature unavailable on this platform (e.g. io_uring on Windows) */
    AEGON_ERR_BUFFER_TOO_SMALL = 8,/* caller-provided output buffer insufficient */
    AEGON_ERR_INTERNAL      = 9    /* uncaught C++ exception reached the firewall; see aegon_last_error() */
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
```

> HTTP **status codes are plain `uint16_t`** (`aegon_res_status(res, 200)`) with
> `AEGON_STATUS_*` macros (below) — avoids a 40-value enum that FFI generators
> mangle, and mirrors `StatusCode`'s underlying type. `aegon_status_phrase()`
> covers any value, including ones Aegon doesn't predefine.

---

## 2.3 `include/aegon/c/error.h` — thread-local diagnostics — **Phase 0**

```c
#ifndef AEGON_C_ERROR_H
#define AEGON_C_ERROR_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* Returns the calling thread's most recent error detail (e.g. the C++ exception's
 * what()). Library-owned, NUL-terminated, overwritten by the next failure on the
 * SAME thread. Never NULL; "" when no error has occurred on this thread.
 * Thread-safe (thread-local storage). */
AEGON_C_API const char* aegon_last_error(void) noexcept;                  /* P0 */

/* Clears the calling thread's error detail. */
AEGON_C_API void aegon_last_error_clear(void) noexcept;                   /* P0 */

/* Static, thread-safe, NUL-terminated description of an error code.
 * Unknown codes return "AEGON_ERR_UNKNOWN". */
AEGON_C_API const char* aegon_err_str(aegon_err_t err) noexcept;          /* P0 */

AEGON_C_END

#endif /* AEGON_C_ERROR_H */
```

Internal contract (`src/c/internal.h`, not public):

```cpp
#define AEGON_C_TRY   try
#define AEGON_C_CATCH                                                     \
    catch (const std::exception& e) {                                     \
        return ::aegon::c::record_error(e.what());                        \
    } catch (...) {                                                       \
        return ::aegon::c::record_error("unknown C++ exception");         \
    }
// record_error() writes the thread-local buffer and returns AEGON_ERR_INTERNAL.
// For functions that can't return aegon_err_t (void fns, pointer-returning
// constructors), a variant records the error and returns nullptr / nothing.
```

---

## 2.4 `include/aegon/c/aegon.h` — umbrella — **Phase 0**

```c
#ifndef AEGON_C_AEGON_H
#define AEGON_C_AEGON_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/error.h>
#include <aegon/c/log.h>          /* P1 */
#include <aegon/c/request.h>      /* P1 */
#include <aegon/c/response.h>     /* P1 */
#include <aegon/c/context.h>      /* P1 */
#include <aegon/c/router.h>       /* P1 */
#include <aegon/c/server.h>       /* P1 */
/* Phase 2: middleware.h cors.h compress.h security.h access_log.h auth.h
 *          static_files.h multipart.h
 * Phase 3: async.h websocket.h sse.h
 * Phase 4: grpc.h
 * (included here as each phase lands; #include order is dependency-free) */

AEGON_C_BEGIN

/* Runtime version of the linked library (may differ from headers). */
AEGON_C_API uint32_t    aegon_version(void) noexcept;                     /* P0 */
AEGON_C_API const char* aegon_version_string(void) noexcept;              /* P0 */

/* ABI epoch actually exported by the linked library (== AEGON_ABI_VERSION). */
AEGON_C_API uint32_t    aegon_abi_version(void) noexcept;                 /* P0 */

/* HTTP status code macros (common subset; any uint16_t is accepted). */
#define AEGON_STATUS_CONTINUE            100u
#define AEGON_STATUS_SWITCHING_PROTOCOLS 101u
#define AEGON_STATUS_OK                  200u
#define AEGON_STATUS_CREATED             201u
#define AEGON_STATUS_ACCEPTED            202u
#define AEGON_STATUS_NO_CONTENT          204u
#define AEGON_STATUS_PARTIAL_CONTENT     206u
#define AEGON_STATUS_MOVED_PERMANENTLY   301u
#define AEGON_STATUS_FOUND               302u
#define AEGON_STATUS_SEE_OTHER           303u
#define AEGON_STATUS_NOT_MODIFIED        304u
#define AEGON_STATUS_TEMPORARY_REDIRECT  307u
#define AEGON_STATUS_PERMANENT_REDIRECT  308u
#define AEGON_STATUS_BAD_REQUEST         400u
#define AEGON_STATUS_UNAUTHORIZED        401u
#define AEGON_STATUS_FORBIDDEN           403u
#define AEGON_STATUS_NOT_FOUND           404u
#define AEGON_STATUS_METHOD_NOT_ALLOWED  405u
#define AEGON_STATUS_REQUEST_TIMEOUT     408u
#define AEGON_STATUS_CONFLICT            409u
#define AEGON_STATUS_PAYLOAD_TOO_LARGE   413u
#define AEGON_STATUS_URI_TOO_LONG        414u
#define AEGON_STATUS_UNPROCESSABLE_ENTITY 422u
#define AEGON_STATUS_UPGRADE_REQUIRED    426u
#define AEGON_STATUS_TOO_MANY_REQUESTS   429u
#define AEGON_STATUS_INTERNAL_SERVER_ERROR 500u
#define AEGON_STATUS_NOT_IMPLEMENTED     501u
#define AEGON_STATUS_BAD_GATEWAY         502u
#define AEGON_STATUS_SERVICE_UNAVAILABLE 503u
#define AEGON_STATUS_GATEWAY_TIMEOUT     504u

/* Reason phrase for any status code ("OK", "Not Found", ...). Static storage.
 * Unknown codes return "Unknown". Mirrors aegon::http::status_phrase. */
AEGON_C_API const char* aegon_status_phrase(uint16_t status) noexcept;    /* P1 */

/* Method <-> string helpers. cstr is static storage ("GET", ...).
 * from_str is case-sensitive per RFC 9110; unknown -> AEGON_METHOD_UNKNOWN. */
AEGON_C_API const char*     aegon_method_cstr(aegon_method_t m) noexcept; /* P1 */
AEGON_C_API aegon_method_t  aegon_method_from_str(aegon_str_t s) noexcept;/* P1 */

/* HTTP version string: static storage ("HTTP/1.1", "HTTP/2.0", "HTTP/3.0"). */
AEGON_C_API const char* aegon_http_version_cstr(aegon_http_version_t v) noexcept; /* P1 */

AEGON_C_END

#endif /* AEGON_C_AEGON_H */
```

---

## 2.5 `include/aegon/c/log.h` — logging subsystem — **Phase 1**

Mirrors `src/log/Logger.h`. Kept deliberately small: C consumers usually have their
own logging; this exists so framework logs can be redirected/leveled and C handlers
can log through the same pipeline.

```c
#ifndef AEGON_C_LOG_H
#define AEGON_C_LOG_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* Initializes the global logger. Optional: lazy defaults are
 * (INFO, synchronous, console sink) if never called.
 * `async` enables the background flush thread (Aegon AsyncQueue).
 * Returns AEGON_ERR_STATE if called twice. */
AEGON_C_API aegon_err_t aegon_log_init(aegon_log_level_t level, bool async_log) noexcept; /* P1 */

/* Runtime level control. Mirrors aegon::log::set_level/get_level. */
AEGON_C_API void aegon_log_set_level(aegon_log_level_t level) noexcept;   /* P1 */
AEGON_C_API aegon_log_level_t aegon_log_level(void) noexcept;             /* P1 */

/* Adds a rolling file sink (append mode). `path` is copied.
 * AEGON_ERR_IO if the file cannot be opened. */
AEGON_C_API aegon_err_t aegon_log_add_file_sink(const char* path) noexcept; /* P1 */

/* Removes all sinks (console + files) — logs go nowhere until a sink is added. */
AEGON_C_API void aegon_log_clear_sinks(void) noexcept;                    /* P1 */

/* Emits a log record through Aegon's pipeline from C code.
 * `msg` may contain any bytes (no printf formatting is applied). */
AEGON_C_API void aegon_log_write(aegon_log_level_t level, aegon_str_t msg) noexcept; /* P1 */

#define aegon_log_trace(m) aegon_log_write(AEGON_LOG_TRACE, (m))
#define aegon_log_debug(m) aegon_log_write(AEGON_LOG_DEBUG, (m))
#define aegon_log_info(m)  aegon_log_write(AEGON_LOG_INFO,  (m))
#define aegon_log_warn(m)  aegon_log_write(AEGON_LOG_WARN,  (m))
#define aegon_log_error(m) aegon_log_write(AEGON_LOG_ERROR, (m))
#define aegon_log_fatal(m) aegon_log_write(AEGON_LOG_FATAL, (m))

/* Flushes buffered records (async mode). */
AEGON_C_API void aegon_log_flush(void) noexcept;                          /* P1 */

/* Flush + stop background thread. Optional at exit; safe to omit. */
AEGON_C_API void aegon_log_shutdown(void) noexcept;                       /* P1 */

AEGON_C_END

#endif /* AEGON_C_LOG_H */
```

---

### Cross-check against C++ sources

| C API | C++ backing | File |
| :--- | :--- | :--- |
| `aegon_version*` | `AEGON_VERSION_*`, `aegon::version` | `src/Version.h` |
| `aegon_method_t` + cstr/from_str | `aegon::http::Method`, `to_string`, `string_to_method` | `src/http/Protocol.h` |
| `aegon_http_version_t` + cstr | `aegon::http::HttpVersion`, `to_string` | `src/http/Protocol.h` |
| `aegon_status_phrase` | `aegon::http::status_phrase` | `src/http/Protocol.h` |
| `aegon_samesite_t` | `aegon::http::SameSite` | `src/http/Cookie.h` |
| `aegon_engine_t` | `aegon::core::EngineType` | `src/core/engine/IoEngine.h` |
| `aegon_log_*` | `aegon::log::{init,set_level,get_level,add_sink,clear_sinks,dispatch,flush,shutdown}` | `src/log/Logger.h` |
