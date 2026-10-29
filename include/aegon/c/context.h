#ifndef AEGON_C_CONTEXT_H
#define AEGON_C_CONTEXT_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/request.h>
#include <aegon/c/response.h>

AEGON_C_BEGIN

typedef struct aegon_ctx aegon_ctx_t;

/* --- Core accessors (zero-cost reinterpret_cast, never NULL inside a handler) --- */
AEGON_C_API aegon_req_t* aegon_ctx_req(aegon_ctx_t* ctx);
AEGON_C_API aegon_res_t* aegon_ctx_res(aegon_ctx_t* ctx);

/* --- Application data (C replacement for Server::provide<T>/ctx.service<T>) ---
 * Set once via aegon_server_set_app_data (04-api-server.md) before run(); readable
 * from every handler on every worker. Not freed by Aegon. NULL if unset. */
AEGON_C_API void* aegon_ctx_app_data(aegon_ctx_t* ctx);

/* --- RFC 7807 Problem Details (Context::problem) ---
 * Sets status, Content-Type: application/problem+json, JSON body with
 * type/title/status/detail/instance. All strings copied. */
AEGON_C_API aegon_err_t aegon_ctx_problem(aegon_ctx_t* ctx, uint16_t status,
                                          aegon_str_t title, aegon_str_t detail);

/* --- File response convenience (Context::send_file = res_file) --- */
AEGON_C_API aegon_err_t aegon_ctx_send_file(aegon_ctx_t* ctx, const char* path,
                                            aegon_str_t mime);

AEGON_C_END

#endif /* AEGON_C_CONTEXT_H */

