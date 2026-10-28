#ifndef AEGON_C_SSE_H
#define AEGON_C_SSE_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/context.h>

AEGON_C_BEGIN

typedef struct aegon_sse aegon_sse_t;   /* connection-bound write channel (thread-safe) */

/* Called on the worker thread when the client disconnects (send failure / close).
 * The stream is dead; call aegon_sse_destroy from any thread afterwards. */
typedef void (*aegon_sse_on_close_fn)(void* userdata);

/* Upgrades the current response to text/event-stream (Context::sse).
 * Must be called inside a handler registered with aegon_router_add_async —
 * the wrapper suspends the coroutine and hands the stream to the caller.
 * Sets Content-Type, Cache-Control, X-Accel-Buffering headers.
 * Returns NULL if the route wasn't registered async (check aegon_last_error). */
AEGON_C_API aegon_sse_t* aegon_sse_create(aegon_ctx_t* ctx,
                                          aegon_sse_on_close_fn on_close,
                                          void* userdata);

/* Senders. THREAD-SAFE from any thread (each send is posted onto the owning
 * event loop — see async.h mechanism). Payload is copied before return. */
AEGON_C_API aegon_err_t aegon_sse_send_event  (aegon_sse_t* sse, aegon_str_t name,
                                               aegon_str_t data);
AEGON_C_API aegon_err_t aegon_sse_send_data   (aegon_sse_t* sse, aegon_str_t data);
AEGON_C_API aegon_err_t aegon_sse_send_comment(aegon_sse_t* sse, aegon_str_t text);
AEGON_C_API aegon_err_t aegon_sse_send_retry  (aegon_sse_t* sse, uint32_t milliseconds);

/* Marks the stream closed (subsequent sends are no-ops). THREAD-SAFE.
 * The connection layer sends the terminal EOF/zero-chunk. */
AEGON_C_API void aegon_sse_close(aegon_sse_t* sse);

AEGON_C_API bool aegon_sse_is_open(aegon_sse_t* sse);

/* Releases the handle (does NOT close the stream — close first if desired).
 * Must eventually be called exactly once. */
AEGON_C_API void aegon_sse_destroy(aegon_sse_t* sse);

AEGON_C_END

#endif /* AEGON_C_SSE_H */

