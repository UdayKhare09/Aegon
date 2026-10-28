#ifndef AEGON_C_RESPONSE_H
#define AEGON_C_RESPONSE_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef struct aegon_res aegon_res_t;

/* --- Status & version (Response::{status,version}) --- */
AEGON_C_API void aegon_res_status(aegon_res_t* res, uint16_t status);

/* --- Headers ---
 * aegon_res_header          : copies name+value into owned storage (safe default).
 * aegon_res_header_static   : zero-copy view; name+value MUST outlive serialization
 *                             (string literals, pinned arenas). */
AEGON_C_API aegon_err_t aegon_res_header(aegon_res_t* res,
                                         aegon_str_t name, aegon_str_t value);
AEGON_C_API void        aegon_res_header_static(aegon_res_t* res,
                                                aegon_str_t name, aegon_str_t value);

/* --- Body (Response::{body,text,json,html,body_view}) ---
 * Copying variants duplicate `data` into the response; caller may free/reuse
 * immediately after return. _static stores a view (see lifetime warning above). */
AEGON_C_API aegon_err_t aegon_res_body(aegon_res_t* res, aegon_str_t data);
AEGON_C_API void        aegon_res_body_static(aegon_res_t* res, aegon_str_t data);

AEGON_C_API aegon_err_t aegon_res_text(aegon_res_t* res, aegon_str_t text);
AEGON_C_API aegon_err_t aegon_res_json(aegon_res_t* res, aegon_str_t json);
AEGON_C_API aegon_err_t aegon_res_html(aegon_res_t* res, aegon_str_t html);

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

AEGON_C_API void        aegon_cookie_options_init(aegon_cookie_options_t* opts);
AEGON_C_API aegon_err_t aegon_res_set_cookie(aegon_res_t* res,
                                             const aegon_cookie_options_t* opts);
/* Expires a cookie immediately (Max-Age=0). path may be {NULL,0} (defaults "/"). */
AEGON_C_API aegon_err_t aegon_res_clear_cookie(aegon_res_t* res,
                                               aegon_str_t name, aegon_str_t path);

/* --- File responses (Response::file — kernel zero-copy sendfile/splice path) ---
 * Sets Content-Type from extension (Response::infer_mime_type) unless mime non-empty.
 * Missing file -> status 404 (C++ behavior). `path` must be NUL-terminated. */
AEGON_C_API aegon_err_t aegon_res_file(aegon_res_t* res, const char* path,
                                       aegon_str_t mime);

/* --- Redirect helper (status + Location header; 3xx values only, else
 * AEGON_ERR_INVALID_ARG). `location` is copied. --- */
AEGON_C_API aegon_err_t aegon_res_redirect(aegon_res_t* res, uint16_t status,
                                           aegon_str_t location);

/* --- MIME utility (Response::infer_mime_type). Static storage. --- */
AEGON_C_API const char* aegon_mime_from_extension(const char* path);

/* --- HTTP/2 & HTTP/3 trailers (Response::trailer / set_trailer_owned).
 * No-op on HTTP/1.1. `trailers_only` maps to Response::trailers_only (gRPC errors). --- */
AEGON_C_API aegon_err_t aegon_res_trailer(aegon_res_t* res,
                                          aegon_str_t name, aegon_str_t value);
AEGON_C_API void        aegon_res_trailer_static(aegon_res_t* res,
                                                 aegon_str_t name, aegon_str_t value);
AEGON_C_API void        aegon_res_trailers_only(aegon_res_t* res, bool on);

AEGON_C_END

#endif /* AEGON_C_RESPONSE_H */

