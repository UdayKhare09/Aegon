#ifndef AEGON_C_ERROR_H
#define AEGON_C_ERROR_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* Returns the calling thread's most recent error detail (e.g. the C++ exception's
 * what()). Library-owned, NUL-terminated, overwritten by the next failure on the
 * SAME thread. Never NULL; "" when no error has occurred on this thread.
 * Thread-safe (thread-local storage). */
AEGON_C_API const char* aegon_last_error(void);

/* Clears the calling thread's error detail. */
AEGON_C_API void aegon_last_error_clear(void);

/* Static, thread-safe, NUL-terminated description of an error code.
 * Unknown codes return "AEGON_ERR_UNKNOWN". */
AEGON_C_API const char* aegon_err_str(aegon_err_t err);

AEGON_C_END

#endif /* AEGON_C_ERROR_H */

