#pragma once

#include <cstdint>
#include <string_view>

namespace aegon::log {

/**
 * @brief Severity levels for logging.
 */
enum class Level : uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5,
    Off   = 6
};

/**
 * @brief Convert Level enum to uppercase string representation.
 */
constexpr std::string_view to_string(Level level) noexcept {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
        case Level::Off:   return "OFF";
    }
    return "UNKNOWN";
}

/**
 * @brief Parse Level from string (case-insensitive).
 */
constexpr Level from_string(std::string_view str, Level default_val = Level::Info) noexcept {
    if (str.empty()) return default_val;

    // Check lowercase or uppercase equivalents
    if (str == "trace" || str == "TRACE" || str == "Trace") return Level::Trace;
    if (str == "debug" || str == "DEBUG" || str == "Debug") return Level::Debug;
    if (str == "info"  || str == "INFO"  || str == "Info")  return Level::Info;
    if (str == "warn"  || str == "WARN"  || str == "Warn" || str == "warning" || str == "WARNING") return Level::Warn;
    if (str == "error" || str == "ERROR" || str == "Error") return Level::Error;
    if (str == "fatal" || str == "FATAL" || str == "Fatal") return Level::Fatal;
    if (str == "off"   || str == "OFF"   || str == "Off")   return Level::Off;

    return default_val;
}

} // namespace aegon::log
