#pragma once

#include "log/Logger.h"
#include <string>
#include <string_view>
#include <format>
#include <source_location>

namespace aegon::http {

/**
 * @brief Contextual logger injected into Context by access_log middleware.
 *
 * Automatically prefixes all log entries with the request's correlation ID,
 * HTTP method, and path for seamless log correlation in production.
 *
 * Usage in handlers:
 * @code
 * // Option A: Pointer style (safe nullable check)
 * if (auto log = ctx.get<RequestLogger>()) {
 *     log->info("Processing order for user {}", user_id);
 * }
 *
 * // Option B: Reference style (throws if missing)
 * ctx.local<RequestLogger>().info("Processing order for user {}", user_id);
 * @endcode
 */
class RequestLogger {
public:
    RequestLogger(std::string_view req_id, std::string_view method, std::string_view path)
        : req_id_(req_id), method_(method), path_(path) {
        if (!req_id_.empty()) {
            prefix_ = std::format("[{}] [{} {}] ", req_id_, method_, path_);
        } else {
            prefix_ = std::format("[{} {}] ", method_, path_);
        }
    }

    [[nodiscard]] std::string_view request_id() const noexcept { return req_id_; }
    [[nodiscard]] std::string_view method() const noexcept { return method_; }
    [[nodiscard]] std::string_view path() const noexcept { return path_; }

    // -----------------------------------------------------------------------
    // Plain string overloads
    // -----------------------------------------------------------------------

    void trace(std::string_view msg, std::source_location loc = std::source_location::current()) const {
        log_impl(log::Level::Trace, loc, msg);
    }

    void debug(std::string_view msg, std::source_location loc = std::source_location::current()) const {
        log_impl(log::Level::Debug, loc, msg);
    }

    void info(std::string_view msg, std::source_location loc = std::source_location::current()) const {
        log_impl(log::Level::Info, loc, msg);
    }

    void warn(std::string_view msg, std::source_location loc = std::source_location::current()) const {
        log_impl(log::Level::Warn, loc, msg);
    }

    void error(std::string_view msg, std::source_location loc = std::source_location::current()) const {
        log_impl(log::Level::Error, loc, msg);
    }

    void fatal(std::string_view msg, std::source_location loc = std::source_location::current()) const {
        log_impl(log::Level::Fatal, loc, msg);
    }

    // -----------------------------------------------------------------------
    // Formatted overloads with compile-time format validation & source_location
    // -----------------------------------------------------------------------

    template <typename... Args>
    void trace(log::FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) const {
        if (log::Level::Trace < log::get_level()) return;
        dispatch_formatted(log::Level::Trace, fl, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(log::FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) const {
        if (log::Level::Debug < log::get_level()) return;
        dispatch_formatted(log::Level::Debug, fl, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(log::FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) const {
        if (log::Level::Info < log::get_level()) return;
        dispatch_formatted(log::Level::Info, fl, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(log::FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) const {
        if (log::Level::Warn < log::get_level()) return;
        dispatch_formatted(log::Level::Warn, fl, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(log::FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) const {
        if (log::Level::Error < log::get_level()) return;
        dispatch_formatted(log::Level::Error, fl, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void fatal(log::FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) const {
        if (log::Level::Fatal < log::get_level()) return;
        dispatch_formatted(log::Level::Fatal, fl, std::forward<Args>(args)...);
    }

private:
    void log_impl(log::Level level, std::source_location loc, std::string_view msg) const {
        if (level < log::get_level()) return;
        std::string full_msg;
        full_msg.reserve(prefix_.size() + msg.size());
        full_msg.append(prefix_);
        full_msg.append(msg);
        log::dispatch(level, loc, std::move(full_msg));
    }

    template <typename... Args>
    void dispatch_formatted(log::Level level,
                            const log::FormatWithLocation<std::type_identity_t<Args>...>& fl,
                            Args&&... args) const {
        std::string formatted = std::vformat(fl.fmt.get(), std::make_format_args(args...));
        std::string full_msg;
        full_msg.reserve(prefix_.size() + formatted.size());
        full_msg.append(prefix_);
        full_msg.append(formatted);
        log::dispatch(level, fl.loc, std::move(full_msg));
    }

    std::string req_id_;
    std::string method_;
    std::string path_;
    std::string prefix_;
};

} // namespace aegon::http
