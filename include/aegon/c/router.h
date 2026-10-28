#ifndef AEGON_C_ROUTER_H
#define AEGON_C_ROUTER_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/context.h>

AEGON_C_BEGIN

typedef struct aegon_router aegon_router_t;  /* ≡ aegon::http::Router  (owned)     */
typedef struct aegon_group  aegon_group_t;   /* holder of aegon::http::RouteGroup  */

/* Synchronous handler: executes through Router::dispatch_sync() (zero coroutine
 * frames, zero heap allocations) — see 01-architecture.md §A7.
 * `ctx` and everything reachable from it is valid ONLY for the duration of the call. */
typedef void (*aegon_handler_fn)(aegon_ctx_t* ctx, void* userdata);

/* Invoked exactly once when the owning router/group/middleware registration is
 * torn down (router destroyed or consumed by server, server destroyed).
 * May be NULL. Order: reverse of registration, after server stop. */
typedef void (*aegon_cleanup_fn)(void* userdata);

/* Error handler (Router::ErrorHandler). Receives the last C++ exception message
 * (may be "" for non-std exceptions). Runs INSTEAD of the default RFC 7807 500. */
typedef void (*aegon_error_handler_fn)(aegon_ctx_t* ctx, aegon_str_t exception_what,
                                       void* userdata);

/* Router lifecycle */
AEGON_C_API aegon_router_t* aegon_router_create(void);
AEGON_C_API void            aegon_router_destroy(aegon_router_t* router);

/* Route registration */
AEGON_C_API aegon_err_t aegon_router_add(aegon_router_t* router,
                                         aegon_method_t method,
                                         const char* pattern,
                                         aegon_handler_fn handler,
                                         void* userdata,
                                         aegon_cleanup_fn cleanup);

AEGON_C_API aegon_err_t aegon_router_get  (aegon_router_t* router, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_router_post (aegon_router_t* router, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_router_put  (aegon_router_t* router, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_router_del  (aegon_router_t* router, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_router_patch(aegon_router_t* router, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_router_all  (aegon_router_t* router, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);

/* Fallback handlers */
AEGON_C_API aegon_err_t aegon_router_on_error(aegon_router_t* router,
                                              aegon_error_handler_fn handler,
                                              void* userdata,
                                              aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_router_on_not_found(aegon_router_t* router,
                                                  aegon_handler_fn handler,
                                                  void* userdata,
                                                  aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_router_on_method_not_allowed(aegon_router_t* router,
                                                           aegon_handler_fn handler,
                                                           void* userdata,
                                                           aegon_cleanup_fn cleanup);

/* Route groups */
AEGON_C_API aegon_group_t* aegon_router_group(aegon_router_t* router,
                                              const char* prefix);
AEGON_C_API aegon_group_t* aegon_group_group(aegon_group_t* parent,
                                             const char* sub_prefix);
AEGON_C_API void           aegon_group_destroy(aegon_group_t* group);

AEGON_C_API aegon_err_t aegon_group_add  (aegon_group_t* group, aegon_method_t method, const char* pattern,
                                          aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_group_get  (aegon_group_t* group, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_group_post (aegon_group_t* group, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_group_put  (aegon_group_t* group, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_group_del  (aegon_group_t* group, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_group_patch(aegon_group_t* group, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_group_all  (aegon_group_t* group, const char* pattern, aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup);

AEGON_C_END

#endif /* AEGON_C_ROUTER_H */

