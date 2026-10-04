#pragma once

#include "http/Context.h"
#include "http/Middleware.h"
#include "http/Protocol.h"
#include "http/RequestLogger.h"
#include "log/Logger.h"
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <format>
#include <algorithm>

namespace aegon::http::middleware {

/**
 * @brief Output formatting style for access logs.
 */
enum class AccessLogFormat : uint8_t {
    Combined,  // [Method] Path Status Duration - ClientIP (Bytes) [Protocol]
    Common,    // ClientIP - [Method Path Protocol] Status Bytes Duration
    Json,      // High-throughput structured ndjson
    Custom     // Developer-supplied custom_formatter
};

/**
 * @brief Comprehensive configuration options for HTTP access logging middleware.
 */
struct AccessLogConfig {
    // 1. Output format style
    AccessLogFormat format{AccessLogFormat::Combined};

    // 2. Severity levels for different response code classes
    log::Level normal_level{log::Level::Info};         // 1xx, 2xx, 3xx
    log::Level client_error_level{log::Level::Warn};    // 4xx
    log::Level server_error_level{log::Level::Error};   // 5xx

    // 3. Performance & Slow Request Highlighting
    std::chrono::nanoseconds slow_threshold{0};        // 0 = disabled. If duration > slow_threshold, logs as slow_level
    log::Level slow_level{log::Level::Warn};            // Level to emit when slow_threshold is exceeded

    // 4. Field Inclusion Controls
    bool log_query{true};          // Append query string if present (e.g. /search?q=term)
    bool log_client_ip{true};      // Append client IP address (from headers or remote)
    bool log_bytes{true};          // Append formatted response payload size (e.g. 1.2 KB)
    bool log_protocol{true};       // Append HTTP protocol version (e.g. HTTP/1.1, HTTP/2.0, HTTP/3.0)

    // 5. Selected Headers to Capture
    std::vector<std::string> request_headers{};        // Request headers to append (e.g. {"user-agent", "x-request-id"})
    std::vector<std::string> response_headers{};       // Response headers to append (e.g. {"content-type"})

    // 6. Path-based Filtering
    std::vector<std::string> skip_paths{};             // Exact paths to skip (e.g. {"/health", "/metrics", "/favicon.ico"})

    // 7. Dynamic Filtering Predicate (return true to skip logging)
    std::function<bool(const Context&)> skip_if{nullptr};

    // 8. Custom Formatter (used when format == AccessLogFormat::Custom or when provided)
    std::function<std::string(const Context&, std::chrono::nanoseconds)> custom_formatter{nullptr};

    // 9. Correlation Header (used to populate RequestLogger in Context)
    std::string correlation_header{"x-request-id"};
};

namespace detail {

inline std::string format_duration(std::chrono::nanoseconds ns) {
    const auto count = ns.count();
    if (count < 1'000) {
        return std::format("{}ns", count);
    } else if (count < 1'000'000) {
        return std::format("{:.1f}µs", static_cast<double>(count) / 1'000.0);
    } else if (count < 1'000'000'000) {
        return std::format("{:.2f}ms", static_cast<double>(count) / 1'000'000.0);
    } else {
        return std::format("{:.2f}s", static_cast<double>(count) / 1'000'000'000.0);
    }
}

inline std::string format_bytes(size_t bytes) {
    if (bytes < 1024) {
        return std::format("{} B", bytes);
    } else if (bytes < 1024 * 1024) {
        return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    } else {
        return std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
}

inline std::string_view extract_client_ip(const Request& req) noexcept {
    if (auto fwd = req.header("x-forwarded-for"); fwd && !fwd->empty()) {
        auto comma = fwd->find(',');
        if (comma != std::string_view::npos) {
            return fwd->substr(0, comma);
        }
        return *fwd;
    }
    if (auto real_ip = req.header("x-real-ip"); real_ip && !real_ip->empty()) {
        return *real_ip;
    }
    return "-";
}

} // namespace detail

/**
 * @brief Factory creating an HTTP Access Logging Middleware.
 *
 * Measures request latency, captures response status codes, formats log records,
 * and emits them through aegon_log.
 *
 * Example:
 * @code
 * router.use(aegon::http::middleware::access_log({
 *     .slow_threshold = std::chrono::milliseconds(50),
 *     .request_headers = {"x-request-id"}
 * }));
 * @endcode
 */
inline MiddlewareFn access_log(AccessLogConfig config = {}) {
    return [config = std::move(config)](Context& ctx, Next next) -> core::Task<void> {
        // Extract correlation ID and inject contextual RequestLogger for route handlers
        std::string_view req_id = "";
        if (!config.correlation_header.empty()) {
            if (auto val = ctx.req().header(config.correlation_header); val) {
                req_id = *val;
            }
        }
        ctx.set<RequestLogger>(RequestLogger(req_id, to_string(ctx.req().method()), ctx.req().path()));

        // Path-based skip check
        std::string_view path = ctx.req().path();
        for (const auto& skip : config.skip_paths) {
            if (path == skip) {
                co_await next(ctx);
                co_return;
            }
        }

        // Custom predicate check
        if (config.skip_if && config.skip_if(ctx)) {
            co_await next(ctx);
            co_return;
        }

        // Measure execution latency
        const auto start = std::chrono::steady_clock::now();
        co_await next(ctx);
        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start);

        // Determine severity level based on status code and latency threshold
        const uint16_t status_num = ctx.res().status_code();
        log::Level level = config.normal_level;
        bool is_slow = false;

        if (status_num >= 500) {
            level = config.server_error_level;
        } else if (status_num >= 400) {
            level = config.client_error_level;
        }

        if (config.slow_threshold.count() > 0 && duration > config.slow_threshold) {
            is_slow = true;
            level = std::max(level, config.slow_level);
        }

        // Fast early return if current global log level is higher than target level
        if (level < log::get_level()) {
            co_return;
        }

        // Custom formatter override
        if (config.custom_formatter) {
            std::string custom_msg = config.custom_formatter(ctx, duration);
            log::dispatch(level, std::source_location::current(), std::move(custom_msg));
            co_return;
        }

        const auto& req = ctx.req();
        const auto& res = ctx.res();
        std::string_view method_str = to_string(req.method());
        std::string_view phrase = status_phrase(res.status());
        size_t body_len = res.has_file() ? res.file_size() : res.body().size();

        std::string line;
        line.reserve(256);

        if (config.format == AccessLogFormat::Json) {
            // High-throughput structured JSON
            std::format_to(std::back_inserter(line),
                "{{\"method\":\"{}\",\"path\":\"{}\",\"status\":{},\"phrase\":\"{}\",\"duration_ns\":{},\"bytes\":{}}}",
                method_str,
                path,
                status_num,
                phrase,
                duration.count(),
                body_len
            );
        } else if (config.format == AccessLogFormat::Common) {
            // CLF-inspired: ClientIP - [Method Path Protocol] Status Bytes Duration
            std::string_view ip = config.log_client_ip ? detail::extract_client_ip(req) : "-";
            std::format_to(std::back_inserter(line),
                "{} - \"{} {} {}\" {} {} ({})",
                ip,
                method_str,
                path,
                to_string(req.version()),
                status_num,
                body_len,
                detail::format_duration(duration)
            );
        } else {
            // Combined format: [SLOW] METHOD /path?query STATUS PHRASE DURATION - IP (BYTES) [PROTOCOL]
            if (is_slow) {
                line.append("[SLOW] ");
            }

            // Method & Path
            line.append(method_str);
            line.push_back(' ');
            line.append(path);

            // Optional Query String
            if (config.log_query && !req.query().empty()) {
                line.push_back('?');
                line.append(req.query());
            }

            // Status Code & Reason Phrase
            std::format_to(std::back_inserter(line), " {} {} {}", status_num, phrase, detail::format_duration(duration));

            // Client IP
            if (config.log_client_ip) {
                line.append(" - ");
                line.append(detail::extract_client_ip(req));
            }

            // Response Payload Size
            if (config.log_bytes) {
                line.append(" (");
                line.append(detail::format_bytes(body_len));
                line.push_back(')');
            }

            // Protocol Version
            if (config.log_protocol) {
                line.append(" [");
                line.append(to_string(req.version()));
                line.push_back(']');
            }

            // Selected Request Headers
            for (const auto& h : config.request_headers) {
                if (auto val = req.header(h); val) {
                    std::format_to(std::back_inserter(line), " [{}: {}]", h, *val);
                }
            }

            // Selected Response Headers
            for (const auto& h : config.response_headers) {
                if (auto val = res.headers().get(h); val) {
                    std::format_to(std::back_inserter(line), " [res:{}: {}]", h, *val);
                }
            }
        }

        log::dispatch(level, std::source_location::current(), std::move(line));
    };
}

/**
 * @brief Semantic alias for access_log().
 */
inline MiddlewareFn logger(AccessLogConfig config = {}) {
    return access_log(std::move(config));
}

} // namespace aegon::http::middleware
