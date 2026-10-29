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

/* --- Asynchronous Middleware Support --- */
typedef struct aegon_async_mw aegon_async_mw_t;

/* Async middleware callback. Runs on the worker thread.
 * May hand mw to an external thread pool or virtual thread.
 * MUST eventually call aegon_async_mw_complete (after optionally calling aegon_async_mw_next). */
typedef void (*aegon_async_middleware_fn)(aegon_ctx_t* ctx,
                                          aegon_async_mw_t* mw,
                                          void* userdata);

/* Advances the chain to downstream middlewares and the route handler.
 * Thread-safe: must be called from an offloaded worker thread or virtual thread.
 * Blocks the calling thread until downstream processing finishes, allowing
 * the caller to perform post-processing on ctx afterwards.
 * Returns AEGON_OK on success, AEGON_ERR_STATE if called twice or on the reactor loop. */
AEGON_C_API aegon_err_t aegon_async_mw_next(aegon_async_mw_t* mw, aegon_ctx_t* ctx);

/* Signals that this async middleware has completed all processing (including post-processing).
 * Must be called exactly once per invocation, whether next was called or short-circuited.
 * Thread-safe: can be called from any thread. */
AEGON_C_API void aegon_async_mw_complete(aegon_async_mw_t* mw);

/* Global async middleware (Router::use_async). */
AEGON_C_API aegon_err_t aegon_router_use_async(aegon_router_t* router,
                                               aegon_async_middleware_fn mw,
                                               void* userdata,
                                               aegon_cleanup_fn cleanup);

/* Group async middleware (RouteGroup::use_async). */
AEGON_C_API aegon_err_t aegon_group_use_async(aegon_group_t* group,
                                              aegon_async_middleware_fn mw,
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

