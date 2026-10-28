#ifndef AEGON_C_ASYNC_H
#define AEGON_C_ASYNC_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/context.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

typedef struct aegon_async aegon_async_t;   /* one-shot completion token */

/* Finalization callback. Runs ON THE OWNING WORKER THREAD after
 * aegon_async_complete() wakes the loop. ctx is valid again for this call only;
 * finalize the response here. After the callback returns, the token and the
 * request are recycled. */
typedef void (*aegon_async_done_fn)(aegon_ctx_t* ctx, void* userdata);

/* Detaches the current request from synchronous dispatch.
 * Must be called from INSIDE a handler registered via aegon_router_add_async
 * (below). On return, the route's C++ coroutine is suspended; the framework will
 * resume it when aegon_async_complete(token) is called from any thread.
 *
 * - One-shot: completing twice is a contract violation.
 * - The caller owns "when" — typical pattern: hand token to a user thread pool,
 *   do the blocking work there, then call aegon_async_complete.
 * - done may be NULL (response finalized before complete; rarely useful). */
AEGON_C_API aegon_async_t* aegon_async_create(aegon_ctx_t* ctx,
                                              aegon_async_done_fn done,
                                              void* userdata);

/* Signals completion. THREAD-SAFE from any thread. Wakes the owning event loop
 * (EventLoop::post); the done callback runs on the worker. */
AEGON_C_API void aegon_async_complete(aegon_async_t* token);

/* Async-capable route registration. Identical to aegon_router_add except the
 * handler may call aegon_async_create. If it doesn't, execution stays fully
 * synchronous (the wrapper falls through to the sync path). */
AEGON_C_API aegon_err_t aegon_router_add_async(aegon_router_t* router,
                                               aegon_method_t method,
                                               const char* pattern,
                                               aegon_handler_fn handler,
                                               void* userdata,
                                               aegon_cleanup_fn cleanup);
AEGON_C_API aegon_err_t aegon_group_add_async(aegon_group_t* group,
                                              aegon_method_t method,
                                              const char* pattern,
                                              aegon_handler_fn handler,
                                              void* userdata,
                                              aegon_cleanup_fn cleanup);

AEGON_C_END

#endif /* AEGON_C_ASYNC_H */

