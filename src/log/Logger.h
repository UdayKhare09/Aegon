#pragma once

#include "log/Level.h"
#include "log/Record.h"
#include "log/Sink.h"
#include "log/ConsoleSink.h"
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <type_traits>
#include <concepts>

namespace aegon::log {

/**
 * @brief Helper that captures both the compile-time format string and the caller's source_location.
 */
template <typename... Args>
struct FormatWithLocation {
    std::format_string<Args...> fmt;
    std::source_location loc;

    template <typename S>
        requires std::constructible_from<std::string_view, const S&>
    consteval FormatWithLocation(const S& s, std::source_location l = std::source_location::current())
        : fmt(s), loc(l) {}
};

/**
 * @brief Configuration parameters for the logging subsystem.
 */
struct LoggerConfig {
    Level level{Level::Info};
    bool async{false};
    size_t flush_interval_ms{10};
    size_t max_queue_size{65536};
};

/**
 * @brief Initializes the global logging system.
 *
 * If not explicitly called, the logger lazily initializes on the first log call
 * with default settings (Level::Info, Synchronous, ConsoleSink).
 */
void init(LoggerConfig config = {});

/**
 * @brief Dynamically set the minimum log severity level.
 */
void set_level(Level level) noexcept;

/**
 * @brief Get the current minimum log severity level.
 */
[[nodiscard]] Level get_level() noexcept;

/**
 * @brief Add a destination sink to the logger.
 */
void add_sink(SinkPtr sink);

/**
 * @brief Replace all sinks with the provided collection.
 */
void set_sinks(std::vector<SinkPtr> sinks);

/**
 * @brief Remove all sinks.
 */
void clear_sinks();

/**
 * @brief Flushes all buffered log messages to their destinations.
 */
void flush();

/**
 * @brief Flushes and shuts down background logging threads.
 */
void shutdown();

/**
 * @brief Internal dispatch function for formatted records.
 */
void dispatch(Level level, std::source_location loc, std::string message);

// ---------------------------------------------------------------------------
// Plain string overloads (preserves raw strings with braces like JSON/regex)
// ---------------------------------------------------------------------------

inline void trace(std::string_view msg, std::source_location loc = std::source_location::current()) {
    if (Level::Trace < get_level()) return;
    dispatch(Level::Trace, loc, std::string(msg));
}

inline void debug(std::string_view msg, std::source_location loc = std::source_location::current()) {
    if (Level::Debug < get_level()) return;
    dispatch(Level::Debug, loc, std::string(msg));
}

inline void info(std::string_view msg, std::source_location loc = std::source_location::current()) {
    if (Level::Info < get_level()) return;
    dispatch(Level::Info, loc, std::string(msg));
}

inline void warn(std::string_view msg, std::source_location loc = std::source_location::current()) {
    if (Level::Warn < get_level()) return;
    dispatch(Level::Warn, loc, std::string(msg));
}

inline void error(std::string_view msg, std::source_location loc = std::source_location::current()) {
    if (Level::Error < get_level()) return;
    dispatch(Level::Error, loc, std::string(msg));
}

inline void fatal(std::string_view msg, std::source_location loc = std::source_location::current()) {
    if (Level::Fatal < get_level()) return;
    dispatch(Level::Fatal, loc, std::string(msg));
}

// ---------------------------------------------------------------------------
// Formatted overloads with compile-time format string validation & source_location
// ---------------------------------------------------------------------------

template <typename... Args>
inline void trace(FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) {
    if (Level::Trace < get_level()) return;
    dispatch(Level::Trace, fl.loc, std::vformat(fl.fmt.get(), std::make_format_args(args...)));
}

template <typename... Args>
inline void debug(FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) {
    if (Level::Debug < get_level()) return;
    dispatch(Level::Debug, fl.loc, std::vformat(fl.fmt.get(), std::make_format_args(args...)));
}

template <typename... Args>
inline void info(FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) {
    if (Level::Info < get_level()) return;
    dispatch(Level::Info, fl.loc, std::vformat(fl.fmt.get(), std::make_format_args(args...)));
}

template <typename... Args>
inline void warn(FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) {
    if (Level::Warn < get_level()) return;
    dispatch(Level::Warn, fl.loc, std::vformat(fl.fmt.get(), std::make_format_args(args...)));
}

template <typename... Args>
inline void error(FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) {
    if (Level::Error < get_level()) return;
    dispatch(Level::Error, fl.loc, std::vformat(fl.fmt.get(), std::make_format_args(args...)));
}

template <typename... Args>
inline void fatal(FormatWithLocation<std::type_identity_t<Args>...> fl, Args&&... args) {
    if (Level::Fatal < get_level()) return;
    dispatch(Level::Fatal, fl.loc, std::vformat(fl.fmt.get(), std::make_format_args(args...)));
}

} // namespace aegon::log

// Convenience macros
#define AEGON_LOG_TRACE(...) ::aegon::log::trace(__VA_ARGS__)
#define AEGON_LOG_DEBUG(...) ::aegon::log::debug(__VA_ARGS__)
#define AEGON_LOG_INFO(...)  ::aegon::log::info(__VA_ARGS__)
#define AEGON_LOG_WARN(...)  ::aegon::log::warn(__VA_ARGS__)
#define AEGON_LOG_ERROR(...) ::aegon::log::error(__VA_ARGS__)
#define AEGON_LOG_FATAL(...) ::aegon::log::fatal(__VA_ARGS__)
