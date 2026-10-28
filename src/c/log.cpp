#include "internal.h"
#include <aegon/c/log.h>
#include "log/Logger.h"
#include "log/FileSink.h"
#include <atomic>

namespace aegon::c {
static std::atomic<bool> s_log_initialized{false};
}

extern "C" {

aegon_err_t aegon_log_init(aegon_log_level_t level, bool async_log) {
    AEGON_C_TRY {
        if (aegon::c::s_log_initialized.exchange(true)) {
            return AEGON_ERR_STATE;
        }
        aegon::log::LoggerConfig cfg;
        cfg.level = static_cast<aegon::log::Level>(level);
        cfg.async = async_log;
        aegon::log::init(cfg);
        return AEGON_OK;
    } AEGON_C_CATCH;
}

void aegon_log_set_level(aegon_log_level_t level) {
    AEGON_C_TRY {
        aegon::log::set_level(static_cast<aegon::log::Level>(level));
    } AEGON_C_CATCH_VOID;
}

aegon_log_level_t aegon_log_level(void) {
    AEGON_C_TRY {
        return static_cast<aegon_log_level_t>(aegon::log::get_level());
    } AEGON_C_CATCH_VAL(AEGON_LOG_INFO);
}

aegon_err_t aegon_log_add_file_sink(const char* path) {
    AEGON_C_TRY {
        if (!path || *path == '\0') {
            return AEGON_ERR_INVALID_ARG;
        }
        auto sink = std::make_shared<aegon::log::FileSink>(path);
        if (!sink->is_open()) {
            return AEGON_ERR_IO;
        }
        aegon::log::add_sink(sink);
        return AEGON_OK;
    } AEGON_C_CATCH;
}

void aegon_log_clear_sinks(void) {
    AEGON_C_TRY {
        aegon::log::clear_sinks();
    } AEGON_C_CATCH_VOID;
}

void aegon_log_write(aegon_log_level_t level, aegon_str_t msg) {
    AEGON_C_TRY {
        if (msg.data && msg.len > 0) {
            aegon::log::dispatch(
                static_cast<aegon::log::Level>(level),
                std::source_location::current(),
                std::string(msg.data, msg.len)
            );
        }
    } AEGON_C_CATCH_VOID;
}

void aegon_log_flush(void) {
    AEGON_C_TRY {
        aegon::log::flush();
    } AEGON_C_CATCH_VOID;
}

void aegon_log_shutdown(void) {
    AEGON_C_TRY {
        aegon::log::shutdown();
        aegon::c::s_log_initialized.store(false);
    } AEGON_C_CATCH_VOID;
}

} // extern "C"

