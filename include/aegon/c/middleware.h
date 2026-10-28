#ifndef AEGON_C_MIDDLEWARE_H
#define AEGON_C_MIDDLEWARE_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/context.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

typedef struct aegon_next aegon_next_t;   /* opaque chain-continuation token */

/* Middleware callback. MUST either:
 *   - call aegon_next_call(next, ctx) exactly once (continue the chain), or
 *   - not call it (short-circuit: response set on ctx is sent as-is).
 * `next` is valid ONLY during this invocation (stack-allocated chain state). */
typedef void (*aegon_middleware_fn)(aegon_ctx_t* ctx, aegon_next_t* next, void* userdata);

/* Continue the onion chain. Safe to call exactly once. After it returns, the
 * response is finalized for this middleware's post-processing. */
AEGON_C_API void aegon_next_call(aegon_next_t* next, aegon_ctx_t* ctx);

/* Global middleware (Router::use). */
AEGON_C_API aegon_err_t aegon_router_use(aegon_router_t* router,
                                         aegon_middleware_fn mw,
                                         void* userdata,
                                         aegon_cleanup_fn cleanup);

/* Group middleware (RouteGroup::use) — runs for every route in the subtree. */
AEGON_C_API aegon_err_t aegon_group_use(aegon_group_t* group,
                                        aegon_middleware_fn mw,
                                        void* userdata,
                                        aegon_cleanup_fn cleanup);

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
                                            aegon_cleanup_fn cleanup);

AEGON_C_API aegon_err_t aegon_group_add_mw(aegon_group_t* group,
                                           aegon_method_t method,
                                           const char* pattern,
                                           const aegon_middleware_slot_t* mws,
                                           size_t mw_count,
                                           aegon_handler_fn handler,
                                           void* userdata,
                                           aegon_cleanup_fn cleanup);

AEGON_C_END

#endif /* AEGON_C_MIDDLEWARE_H */

