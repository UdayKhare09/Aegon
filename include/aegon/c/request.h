#ifndef AEGON_C_REQUEST_H
#define AEGON_C_REQUEST_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

typedef struct aegon_req aegon_req_t;

/* --- Line basics (Request::{method,path,query,body,version}) --- */
AEGON_C_API aegon_method_t       aegon_req_method(aegon_req_t* req);
AEGON_C_API aegon_http_version_t aegon_req_version(aegon_req_t* req);
AEGON_C_API aegon_str_t          aegon_req_path(aegon_req_t* req);
AEGON_C_API aegon_str_t          aegon_req_query_string(aegon_req_t* req);
AEGON_C_API aegon_str_t          aegon_req_body(aegon_req_t* req);

/* --- Headers (HeaderMap: case-insensitive, <= 32 inline) ---
 * get: first match, {NULL,0} if absent. count/at: index iteration in arrival order.
 * `at` returns false when index is out of range (out_name/out_value untouched). */
AEGON_C_API aegon_str_t aegon_req_header(aegon_req_t* req, aegon_str_t name);
AEGON_C_API bool        aegon_req_has_header(aegon_req_t* req, aegon_str_t name);
AEGON_C_API size_t      aegon_req_header_count(aegon_req_t* req);
AEGON_C_API bool        aegon_req_header_at(aegon_req_t* req, size_t index,
                                            aegon_str_t* out_name,
                                            aegon_str_t* out_value);

/* --- Route parameters (RadixTree :params, e.g. /users/:id) ---
 * Request::param; {NULL,0} when absent. */
AEGON_C_API aegon_str_t aegon_req_param(aegon_req_t* req, aegon_str_t key);
AEGON_C_API size_t      aegon_req_param_count(aegon_req_t* req);
AEGON_C_API bool        aegon_req_param_at(aegon_req_t* req, size_t index,
                                           aegon_str_t* out_key,
                                           aegon_str_t* out_value);

/* --- Query string (?a=1&b=2) ---
 * Request::query_param — linear scan, zero-alloc, value is NOT url-decoded
 * (matches C++ behavior). */
AEGON_C_API aegon_str_t aegon_req_query(aegon_req_t* req, aegon_str_t key);

/* --- Cookies (Request::cookie — parses Cookie header, unquotes) ---
 * {NULL,0} when absent. */
AEGON_C_API aegon_str_t aegon_req_cookie(aegon_req_t* req, aegon_str_t name);

/* --- Flags --- */
AEGON_C_API bool aegon_req_is_grpc(aegon_req_t* req);
AEGON_C_API bool aegon_req_is_websocket(aegon_req_t* req);

/* --- Utilities ---
 * URL-decodes `in` into a fresh Aegon-allocated buffer (Request::url_decode).
 * Free with aegon_mem_free(). */
AEGON_C_API aegon_str_t aegon_url_decode(aegon_str_t in);

AEGON_C_END

#endif /* AEGON_C_REQUEST_H */

