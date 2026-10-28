#ifndef AEGON_C_LOG_H
#define AEGON_C_LOG_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>

AEGON_C_BEGIN

/* Initializes the global logger. Optional: lazy defaults are
 * (INFO, synchronous, console sink) if never called.
 * `async_log` enables the background flush thread (Aegon AsyncQueue).
 * Returns AEGON_ERR_STATE if called twice. */
AEGON_C_API aegon_err_t aegon_log_init(aegon_log_level_t level, bool async_log);

/* Runtime level control. Mirrors aegon::log::set_level/get_level. */
AEGON_C_API void aegon_log_set_level(aegon_log_level_t level);
AEGON_C_API aegon_log_level_t aegon_log_level(void);

/* Adds a rolling file sink (append mode). `path` is copied.
 * AEGON_ERR_IO if the file cannot be opened. */
AEGON_C_API aegon_err_t aegon_log_add_file_sink(const char* path);

/* Removes all sinks (console + files) — logs go nowhere until a sink is added. */
AEGON_C_API void aegon_log_clear_sinks(void);

/* Emits a log record through Aegon's pipeline from C code.
 * `msg` may contain any bytes (no printf formatting is applied). */
AEGON_C_API void aegon_log_write(aegon_log_level_t level, aegon_str_t msg);

#define aegon_log_trace(m) aegon_log_write(AEGON_LOG_TRACE, (m))
#define aegon_log_debug(m) aegon_log_write(AEGON_LOG_DEBUG, (m))
#define aegon_log_info(m)  aegon_log_write(AEGON_LOG_INFO,  (m))
#define aegon_log_warn(m)  aegon_log_write(AEGON_LOG_WARN,  (m))
#define aegon_log_error(m) aegon_log_write(AEGON_LOG_ERROR, (m))
#define aegon_log_fatal(m) aegon_log_write(AEGON_LOG_FATAL, (m))

/* Flushes buffered records (async mode). */
AEGON_C_API void aegon_log_flush(void);

/* Flush + stop background thread. Optional at exit; safe to omit. */
AEGON_C_API void aegon_log_shutdown(void);

AEGON_C_END

#endif /* AEGON_C_LOG_H */

