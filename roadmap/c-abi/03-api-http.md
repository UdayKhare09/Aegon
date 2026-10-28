# 03 — HTTP Data API: `request.h`, `response.h`, `context.h`, `multipart.h`

All per-request functions are **thread-confined** to the worker executing the handler
(see `01-architecture.md` §A6) and **zero-allocation** on the read path.

Opaque handles declared here:

```c
typedef struct aegon_ctx aegon_ctx_t;    /* ≡ aegon::http::Context   */
typedef struct aegon_req aegon_req_t;    /* ≡ aegon::http::Request   */
typedef struct aegon_res aegon_res_t;    /* ≡ aegon::http::Response  */
```

---

## 3.1 `include/aegon/c/request.h` — request inspection — **Phase 1**

C++ backing: `src/http/Request.h`, `src/http/HeaderMap.h`.
Every getter returns a slice into connection memory; **do not free, do not retain
beyond handler return** (lifetime table in `01-architecture.md` §A5).

```c
#ifndef AEGON_C_REQUEST_H
#define AEGON_C_REQUEST_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* --- Line basics (Request::{method,path,query,body,version}) --- */
AEGON_C_API aegon_method_t       aegon_req_method(aegon_req_t* req) noexcept;        /* P1 */
AEGON_C_API aegon_http_version_t aegon_req_version(aegon_req_t* req) noexcept;       /* P1 */
AEGON_C_API aegon_str_t          aegon_req_path(aegon_req_t* req) noexcept;          /* P1 */
AEGON_C_API aegon_str_t          aegon_req_query_string(aegon_req_t* req) noexcept;  /* P1 */
AEGON_C_API aegon_str_t          aegon_req_body(aegon_req_t* req) noexcept;          /* P1 */

/* --- Headers (HeaderMap: case-insensitive, ≤32 inline) ---
 * get: first match, {NULL,0} if absent. count/at: index iteration in arrival order.
 * `at` returns false when index is out of range (out_name/out_value untouched). */
AEGON_C_API aegon_str_t aegon_req_header(aegon_req_t* req, aegon_str_t name) noexcept; /* P1 */
AEGON_C_API bool        aegon_req_has_header(aegon_req_t* req, aegon_str_t name) noexcept; /* P1 */
AEGON_C_API size_t      aegon_req_header_count(aegon_req_t* req) noexcept;           /* P1 */
AEGON_C_API bool        aegon_req_header_at(aegon_req_t* req, size_t index,
                                            aegon_str_t* out_name,
                                            aegon_str_t* out_value) noexcept;        /* P1 */

/* --- Route parameters (RadixTree :params, e.g. /users/:id) ---
 * Request::param; {NULL,0} when absent. */
AEGON_C_API aegon_str_t aegon_req_param(aegon_req_t* req, aegon_str_t key) noexcept; /* P1 */
AEGON_C_API size_t      aegon_req_param_count(aegon_req_t* req) noexcept;            /* P1 */
AEGON_C_API bool        aegon_req_param_at(aegon_req_t* req, size_t index,
                                           aegon_str_t* out_key,
                                           aegon_str_t* out_value) noexcept;         /* P1 */

/* --- Query string (?a=1&b=2) ---
 * Request::query_param — linear scan, zero-alloc, value is NOT url-decoded
 * (matches C++ behavior). */
AEGON_C_API aegon_str_t aegon_req_query(aegon_req_t* req, aegon_str_t key) noexcept; /* P1 */

/* --- Cookies (Request::cookie — parses Cookie header, unquotes) ---
 * {NULL,0} when absent. */
AEGON_C_API aegon_str_t aegon_req_cookie(aegon_req_t* req, aegon_str_t name) noexcept; /* P1 */

/* --- Flags --- */
AEGON_C_API bool aegon_req_is_grpc(aegon_req_t* req) noexcept;                       /* P1 */
AEGON_C_API bool aegon_req_is_websocket(aegon_req_t* req) noexcept;                  /* P1 */

/* --- Utilities ---
 * URL-decodes `in` into a fresh Aegon-allocated buffer (Request::url_decode).
 * Free with aegon_mem_free(). */
AEGON_C_API aegon_str_t aegon_url_decode(aegon_str_t in) noexcept;                   /* P1 */

AEGON_C_END

#endif /* AEGON_C_REQUEST_H */
```

---

## 3.2 `include/aegon/c/response.h` — response mutation — **Phase 1**

C++ backing: `src/http/Response.h`, `src/http/Cookie.h`.
Ownership contract (`01-architecture.md` §A2): plain setters **copy**; `*_static`
setters store a **view** and require pinned memory.

```c
#ifndef AEGON_C_RESPONSE_H
#define AEGON_C_RESPONSE_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* --- Status & version (Response::{status,version}) --- */
AEGON_C_API void aegon_res_status(aegon_res_t* res, uint16_t status) noexcept;       /* P1 */

/* --- Headers ---
 * aegon_res_header          : copies name+value into owned storage (safe default).
 * aegon_res_header_static   : zero-copy view; name+value MUST outlive serialization
 *                             (string literals, pinned arenas). */
AEGON_C_API aegon_err_t aegon_res_header(aegon_res_t* res,
                                         aegon_str_t name, aegon_str_t value) noexcept; /* P1 */
AEGON_C_API void        aegon_res_header_static(aegon_res_t* res,
                                                aegon_str_t name, aegon_str_t value) noexcept; /* P1 */

/* --- Body (Response::{body,text,json,html,body_view}) ---
 * Copying variants duplicate `data` into the response; caller may free/reuse
 * immediately after return. _static stores a view (see lifetime warning above). */
AEGON_C_API aegon_err_t aegon_res_body(aegon_res_t* res, aegon_str_t data) noexcept; /* P1 */
AEGON_C_API void        aegon_res_body_static(aegon_res_t* res, aegon_str_t data) noexcept; /* P1 */

AEGON_C_API aegon_err_t aegon_res_text(aegon_res_t* res, aegon_str_t text) noexcept; /* P1 (sets Content-Type: text/plain) */
AEGON_C_API aegon_err_t aegon_res_json(aegon_res_t* res, aegon_str_t json) noexcept; /* P1 (sets application/json) */
AEGON_C_API aegon_err_t aegon_res_html(aegon_res_t* res, aegon_str_t html) noexcept; /* P1 (sets text/html; charset=utf-8) */

/* --- Cookies (Response::set_cookie / clear_cookie) ---
 * The struct is fully copied at call time (01-architecture.md §A8). */
typedef struct aegon_cookie_options {
    size_t           struct_size;     /* = sizeof(aegon_cookie_options_t) */
    aegon_str_t      name;            /* required */
    aegon_str_t      value;           /* may be empty */
    aegon_str_t      path;            /* empty -> no Path attribute (C++ default is "/") */
    aegon_str_t      domain;          /* empty -> omitted */
    int64_t          max_age_seconds; /* <0 -> omitted (C++ nullopt) */
    aegon_samesite_t same_site;
    bool             http_only;
    bool             secure;
    bool             partitioned;     /* CHIPS */
} aegon_cookie_options_t;

AEGON_C_API void aegon_cookie_options_init(aegon_cookie_options_t* opts) noexcept;   /* P1 */
AEGON_C_API aegon_err_t aegon_res_set_cookie(aegon_res_t* res,
                                             const aegon_cookie_options_t* opts) noexcept; /* P1 */
/* Expires a cookie immediately (Max-Age=0). path may be {NULL,0} (defaults "/"). */
AEGON_C_API aegon_err_t aegon_res_clear_cookie(aegon_res_t* res,
                                               aegon_str_t name, aegon_str_t path) noexcept; /* P1 */

/* --- File responses (Response::file — kernel zero-copy sendfile/splice path) ---
 * Sets Content-Type from extension (Response::infer_mime_type) unless mime non-empty.
 * Missing file -> status 404 (C++ behavior). `path` must be NUL-terminated. */
AEGON_C_API aegon_err_t aegon_res_file(aegon_res_t* res, const char* path,
                                       aegon_str_t mime) noexcept;                  /* P1 */

/* --- Redirect helper (status + Location header; 3xx values only, else
 * AEGON_ERR_INVALID_ARG). `location` is copied. --- */
AEGON_C_API aegon_err_t aegon_res_redirect(aegon_res_t* res, uint16_t status,
                                           aegon_str_t location) noexcept;          /* P1 */

/* --- MIME utility (Response::infer_mime_type). Static storage. --- */
AEGON_C_API const char* aegon_mime_from_extension(const char* path) noexcept;        /* P1 */

/* --- HTTP/2 & HTTP/3 trailers (Response::trailer / set_trailer_owned).
 * No-op on HTTP/1.1. `trailers_only` maps to Response::trailers_only (gRPC errors). --- */
AEGON_C_API aegon_err_t aegon_res_trailer(aegon_res_t* res,
                                          aegon_str_t name, aegon_str_t value) noexcept; /* P1 (copying) */
AEGON_C_API void        aegon_res_trailer_static(aegon_res_t* res,
                                                 aegon_str_t name, aegon_str_t value) noexcept; /* P1 */
AEGON_C_API void        aegon_res_trailers_only(aegon_res_t* res, bool on) noexcept; /* P1 */

AEGON_C_END

#endif /* AEGON_C_RESPONSE_H */
```

---

## 3.3 `include/aegon/c/context.h` — per-request context glue — **Phase 1**

C++ backing: `src/http/Context.h`.

```c
#ifndef AEGON_C_CONTEXT_H
#define AEGON_C_CONTEXT_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* --- Core accessors (zero-cost reinterpret_cast, never NULL inside a handler) --- */
AEGON_C_API aegon_req_t* aegon_ctx_req(aegon_ctx_t* ctx) noexcept;                   /* P1 */
AEGON_C_API aegon_res_t* aegon_ctx_res(aegon_ctx_t* ctx) noexcept;                   /* P1 */

/* --- Application data (C replacement for Server::provide<T>/ctx.service<T>) ---
 * Set once via aegon_server_set_app_data (04-api-server.md) before run(); readable
 * from every handler on every worker. Not freed by Aegon. NULL if unset. */
AEGON_C_API void* aegon_ctx_app_data(aegon_ctx_t* ctx) noexcept;                     /* P1 */

/* --- Per-request key/value locals (C replacement for ctx.set<T>/get<T>) ---
 * Backed by a lazily-created map; keys are C strings (case-sensitive).
 * Values are opaque void*; Aegon does not free them. Lifetime: single request.
 * get returns NULL when absent. */
AEGON_C_API aegon_err_t aegon_ctx_local_set(aegon_ctx_t* ctx, const char* key,
                                            void* value) noexcept;                   /* P1 */
AEGON_C_API void*       aegon_ctx_local_get(aegon_ctx_t* ctx, const char* key) noexcept; /* P1 */

/* --- RFC 7807 Problem Details (Context::problem) ---
 * Sets status, Content-Type: application/problem+json, JSON body with
 * type/title/status/detail/instance. All strings copied. */
AEGON_C_API aegon_err_t aegon_ctx_problem(aegon_ctx_t* ctx, uint16_t status,
                                          aegon_str_t title, aegon_str_t detail) noexcept; /* P1 */

/* --- File response convenience (Context::send_file ≡ res_file) --- */
AEGON_C_API aegon_err_t aegon_ctx_send_file(aegon_ctx_t* ctx, const char* path,
                                            aegon_str_t mime) noexcept;              /* P1 */

AEGON_C_END

#endif /* AEGON_C_CONTEXT_H */
```

---

## 3.4 `include/aegon/c/multipart.h` — `multipart/form-data` — **Phase 2**

C++ backing: `src/http/MultipartParser.h` (zero-copy parser — all field slices point
into the request body).

```c
#ifndef AEGON_C_MULTIPART_H
#define AEGON_C_MULTIPART_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef struct aegon_multipart aegon_multipart_t;   /* owns MultipartFormData + body copy flag */

/* Parses ctx's body using the request Content-Type boundary.
 * Returns NULL when Content-Type is not multipart or the body is malformed
 * (check aegon_last_error). The result borrows the body bytes — do not let it
 * outlive the handler. Free with aegon_multipart_destroy. */
AEGON_C_API aegon_multipart_t* aegon_multipart_parse(aegon_ctx_t* ctx) noexcept;     /* P2 */
AEGON_C_API void               aegon_multipart_destroy(aegon_multipart_t* mp) noexcept; /* P2 */

/* Field lookup (MultipartFormData::get). {NULL,0} when absent. */
AEGON_C_API aegon_str_t aegon_multipart_field(aegon_multipart_t* mp,
                                              aegon_str_t name) noexcept;            /* P2 */
AEGON_C_API size_t      aegon_multipart_field_count(aegon_multipart_t* mp) noexcept; /* P2 */
AEGON_C_API bool        aegon_multipart_field_at(aegon_multipart_t* mp, size_t index,
                                                 aegon_str_t* out_name,
                                                 aegon_str_t* out_value) noexcept;   /* P2 */

/* File lookup (MultipartFormData::file). Any out_* may be NULL.
 * Returns false when no file is attached under `name`. */
AEGON_C_API bool aegon_multipart_file(aegon_multipart_t* mp, aegon_str_t name,
                                      aegon_str_t* out_filename,
                                      aegon_str_t* out_content_type,
                                      aegon_str_t* out_data) noexcept;               /* P2 */
AEGON_C_API size_t aegon_multipart_file_count(aegon_multipart_t* mp) noexcept;       /* P2 */
AEGON_C_API bool   aegon_multipart_file_at(aegon_multipart_t* mp, size_t index,
                                           aegon_str_t* out_name,
                                           aegon_str_t* out_filename,
                                           aegon_str_t* out_content_type,
                                           aegon_str_t* out_data) noexcept;          /* P2 */

AEGON_C_END

#endif /* AEGON_C_MULTIPART_H */
```

---

### Cross-check against C++ sources

| C API | C++ backing |
| :--- | :--- |
| `aegon_req_method/path/query_string/body/version` | `Request::{method,path,query,body,version}` |
| `aegon_req_header*` | `HeaderMap::{get,contains}` + inline/heap entry iteration |
| `aegon_req_param*`, `aegon_req_query`, `aegon_req_cookie` | `Request::{param,query_param,cookie}` |
| `aegon_url_decode` | `Request::url_decode` |
| `aegon_res_header(_static)` | `Response::set_header_owned` / `Response::header` |
| `aegon_res_body(_static)/text/json/html` | `Response::{body,body_view,text,json,html}` |
| `aegon_cookie_options_t`, `aegon_res_set/clear_cookie` | `CookieOptions`, `Response::{set_cookie,clear_cookie}` |
| `aegon_res_file`, `aegon_mime_from_extension` | `Response::{file,infer_mime_type}` |
| `aegon_res_trailer*`, `aegon_res_trailers_only` | `Response::{set_trailer_owned,trailer,trailers_only}` |
| `aegon_ctx_problem` | `Context::problem` (`ProblemDetails::to_json`) |
| `aegon_ctx_local_*` | new thin map wrapper (C++ `ctx.set<T>` is typeid-keyed, unavailable to C) |
| `aegon_multipart_*` | `MultipartParser::parse`, `MultipartFormData::{get,file,fields,files}` |
