#include "log/ConsoleSink.h"
#include <unistd.h>
#include <iostream>
#include <format>
#include <ctime>

namespace aegon::log {

namespace {

constexpr std::string_view COLOR_RESET  = "\033[0m";
constexpr std::string_view COLOR_DIM    = "\033[90m";
constexpr std::string_view COLOR_TRACE  = "\033[90m";
constexpr std::string_view COLOR_DEBUG  = "\033[36m";
constexpr std::string_view COLOR_INFO   = "\033[32m";
constexpr std::string_view COLOR_WARN   = "\033[33m";
constexpr std::string_view COLOR_ERROR  = "\033[31m";
constexpr std::string_view COLOR_FATAL  = "\033[1;31m";

std::string_view level_color(Level level) noexcept {
    switch (level) {
        case Level::Trace: return COLOR_TRACE;
        case Level::Debug: return COLOR_DEBUG;
        case Level::Info:  return COLOR_INFO;
        case Level::Warn:  return COLOR_WARN;
        case Level::Error: return COLOR_ERROR;
        case Level::Fatal: return COLOR_FATAL;
        default:           return COLOR_RESET;
    }
}

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
    auto tp_millis = std::chrono::floor<std::chrono::milliseconds>(tp);
    auto secs = std::chrono::floor<std::chrono::seconds>(tp_millis);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(tp_millis - secs).count();

    std::time_t t = std::chrono::system_clock::to_time_t(secs);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
                       tm_buf.tm_year + 1900,
                       tm_buf.tm_mon + 1,
                       tm_buf.tm_mday,
                       tm_buf.tm_hour,
                       tm_buf.tm_min,
                       tm_buf.tm_sec,
                       millis);
}

} // namespace

ConsoleSink::ConsoleSink(ConsoleSinkConfig config)
    : config_(config) {
    if (config_.color_mode == ColorMode::Always) {
        use_colors_ = true;
    } else if (config_.color_mode == ColorMode::Never) {
        use_colors_ = false;
    } else {
        use_colors_ = isatty(STDOUT_FILENO) != 0;
    }
}

void ConsoleSink::log(const Record& record) {
    std::string ts = format_timestamp(record.timestamp);
    std::string_view lvl_str = to_string(record.level);

    std::string buffer;
    buffer.reserve(256 + record.message.size());

    if (use_colors_) {
        // [Timestamp]
        std::format_to(std::back_inserter(buffer), "{}[{}]{} ",
                       COLOR_DIM, ts, COLOR_RESET);

        // [LEVEL]
        std::format_to(std::back_inserter(buffer), "{}[{:5}]{} ",
                       level_color(record.level), lvl_str, COLOR_RESET);

        // Optional Thread ID
        if (config_.show_thread_id) {
            std::format_to(std::back_inserter(buffer), "{}[t:{:x}]{} ",
                           COLOR_DIM, record.thread_id, COLOR_RESET);
        }

        // [location]
        if (config_.show_location && record.location.line() > 0) {
            std::format_to(std::back_inserter(buffer), "{}[{}:{}]{} ",
                           COLOR_DIM, record.file_basename(), record.location.line(), COLOR_RESET);
        }

        // Message
        buffer.append(record.message);
        buffer.push_back('\n');
    } else {
        // Plain text without ANSI escapes
        std::format_to(std::back_inserter(buffer), "[{}] [{:5}] ", ts, lvl_str);

        if (config_.show_thread_id) {
            std::format_to(std::back_inserter(buffer), "[t:{:x}] ", record.thread_id);
        }

        if (config_.show_location && record.location.line() > 0) {
            std::format_to(std::back_inserter(buffer), "[{}:{}] ",
                           record.file_basename(), record.location.line());
        }

        buffer.append(record.message);
        buffer.push_back('\n');
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (config_.stderr_for_errors && record.level >= Level::Error) {
        std::cerr.write(buffer.data(), buffer.size());
        std::cerr.flush();
    } else {
        std::cout.write(buffer.data(), buffer.size());
        std::cout.flush();
    }
}

void ConsoleSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout.flush();
    if (config_.stderr_for_errors) {
        std::cerr.flush();
    }
}

} // namespace aegon::log
