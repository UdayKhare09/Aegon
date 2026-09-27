#pragma once

#include "core/simd/SimdString.h"
#include "http/ServerConfig.h"
#include <cstdint>
#include <string_view>
#include <string>
#include <optional>
#include <cctype>
#include <ctime>

namespace aegon::http {

enum class HttpVersion : uint8_t {
    Http1_0,
    Http1_1,
    Http2,
    Http3
};

constexpr std::string_view to_string(HttpVersion v) noexcept {
    switch (v) {
        case HttpVersion::Http1_0: return "HTTP/1.0";
        case HttpVersion::Http1_1: return "HTTP/1.1";
        case HttpVersion::Http2:   return "HTTP/2.0";
        case HttpVersion::Http3:   return "HTTP/3.0";
    }
    return "HTTP/1.1";
}

enum class Method : uint8_t {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    HEAD,
    OPTIONS,
    CONNECT,
    TRACE,
    UNKNOWN
};

constexpr std::string_view to_string(Method m) noexcept {
    switch (m) {
        case Method::GET:     return "GET";
        case Method::POST:    return "POST";
        case Method::PUT:     return "PUT";
        case Method::DELETE:  return "DELETE";
        case Method::PATCH:   return "PATCH";
        case Method::HEAD:    return "HEAD";
        case Method::OPTIONS: return "OPTIONS";
        case Method::CONNECT: return "CONNECT";
        case Method::TRACE:   return "TRACE";
        case Method::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

constexpr Method string_to_method(std::string_view sv) noexcept {
    if (sv == "GET") return Method::GET;
    if (sv == "POST") return Method::POST;
    if (sv == "PUT") return Method::PUT;
    if (sv == "DELETE") return Method::DELETE;
    if (sv == "PATCH") return Method::PATCH;
    if (sv == "HEAD") return Method::HEAD;
    if (sv == "OPTIONS") return Method::OPTIONS;
    if (sv == "CONNECT") return Method::CONNECT;
    if (sv == "TRACE") return Method::TRACE;
    return Method::UNKNOWN;
}

enum class StatusCode : uint16_t {
    // 1xx Informational
    Continue = 100,
    SwitchingProtocols = 101,

    // 2xx Success
    Ok = 200,
    Created = 201,
    Accepted = 202,
    NoContent = 204,
    ResetContent = 205,
    PartialContent = 206,

    // 3xx Redirection
    MultipleChoices = 300,
    MovedPermanently = 301,
    Found = 302,
    SeeOther = 303,
    NotModified = 304,
    TemporaryRedirect = 307,
    PermanentRedirect = 308,

    // 4xx Client Error
    BadRequest = 400,
    Unauthorized = 401,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    NotAcceptable = 406,
    RequestTimeout = 408,
    Conflict = 409,
    Gone = 410,
    LengthRequired = 411,
    PayloadTooLarge = 413,
    UriTooLong = 414,
    UnsupportedMediaType = 415,
    ExpectationFailed = 417,
    UnprocessableEntity = 422,
    UpgradeRequired = 426,
    TooManyRequests = 429,
    RequestHeaderFieldsTooLarge = 431,

    // 5xx Server Error
    InternalServerError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503,
    GatewayTimeout = 504,
    HttpVersionNotSupported = 505
};

constexpr std::string_view status_phrase(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::Continue: return "Continue";
        case StatusCode::SwitchingProtocols: return "Switching Protocols";
        case StatusCode::Ok: return "OK";
        case StatusCode::Created: return "Created";
        case StatusCode::Accepted: return "Accepted";
        case StatusCode::NoContent: return "No Content";
        case StatusCode::ResetContent: return "Reset Content";
        case StatusCode::PartialContent: return "Partial Content";
        case StatusCode::MultipleChoices: return "Multiple Choices";
        case StatusCode::MovedPermanently: return "Moved Permanently";
        case StatusCode::Found: return "Found";
        case StatusCode::SeeOther: return "See Other";
        case StatusCode::NotModified: return "Not Modified";
        case StatusCode::TemporaryRedirect: return "Temporary Redirect";
        case StatusCode::PermanentRedirect: return "Permanent Redirect";
        case StatusCode::BadRequest: return "Bad Request";
        case StatusCode::Unauthorized: return "Unauthorized";
        case StatusCode::Forbidden: return "Forbidden";
        case StatusCode::NotFound: return "Not Found";
        case StatusCode::MethodNotAllowed: return "Method Not Allowed";
        case StatusCode::NotAcceptable: return "Not Acceptable";
        case StatusCode::RequestTimeout: return "Request Timeout";
        case StatusCode::Conflict: return "Conflict";
        case StatusCode::Gone: return "Gone";
        case StatusCode::LengthRequired: return "Length Required";
        case StatusCode::PayloadTooLarge: return "Payload Too Large";
        case StatusCode::UriTooLong: return "URI Too Long";
        case StatusCode::UnsupportedMediaType: return "Unsupported Media Type";
        case StatusCode::ExpectationFailed: return "Expectation Failed";
        case StatusCode::UnprocessableEntity: return "Unprocessable Entity";
        case StatusCode::UpgradeRequired: return "Upgrade Required";
        case StatusCode::TooManyRequests: return "Too Many Requests";
        case StatusCode::RequestHeaderFieldsTooLarge: return "Request Header Fields Too Large";
        case StatusCode::InternalServerError: return "Internal Server Error";
        case StatusCode::NotImplemented: return "Not Implemented";
        case StatusCode::BadGateway: return "Bad Gateway";
        case StatusCode::ServiceUnavailable: return "Service Unavailable";
        case StatusCode::GatewayTimeout: return "Gateway Timeout";
        case StatusCode::HttpVersionNotSupported: return "HTTP Version Not Supported";
    }
    return "Unknown";
}

// Aegon Default Protocol Limits
inline constexpr ProtocolLimits DEFAULT_LIMITS{};

/**
 * @brief Checks if a header is hop-by-hop (prohibited in HTTP/2 RFC 9113 §8.2.2 and HTTP/3 RFC 9114 §4.2)
 */
inline bool is_hop_by_hop_header(std::string_view name) noexcept {
    return core::simd::SimdString::iequals(name, "connection") ||
           core::simd::SimdString::iequals(name, "keep-alive") ||
           core::simd::SimdString::iequals(name, "transfer-encoding") ||
           core::simd::SimdString::iequals(name, "upgrade");
}

/**
 * @brief Returns the current HTTP-date formatted string per RFC 9110 §5.6.7 (e.g. "Sun, 06 Nov 1994 08:49:37 GMT").
 * Cached per thread per second to avoid repeated strftime calls across H1, H2, and H3.
 */
inline std::string_view get_http_date() noexcept {
    static thread_local time_t last_time = 0;
    static thread_local char date_buf[64];
    static thread_local size_t date_len = 0;
    time_t now = time(nullptr);
    if (now != last_time) {
        last_time = now;
        struct tm gmt;
        gmtime_r(&now, &gmt);
        date_len = strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
    }
    return {date_buf, date_len};
}

/**
 * @brief Checks if status code mandates suppression of Content-Length (RFC 9110 §8.6)
 */
inline bool should_suppress_content_length(StatusCode code) noexcept {
    uint16_t sc = static_cast<uint16_t>(code);
    return (sc >= 100 && sc < 200) || sc == 204 || sc == 304;
}

/**
 * @brief Fast ASCII lowercase conversion
 */
inline std::string to_lower_ascii(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

/**
 * @brief Validates request target / path per RFC 9112 §3.2, RFC 9113 §8.3.1, RFC 9114 §4.3.1
 * Rejects URI fragments (#), percent-encoded NUL (%00), and percent-encoded CRLF (%0d, %0a).
 */
inline bool is_valid_request_target(std::string_view target) noexcept {
    if (target.empty()) return false;
    for (size_t i = 0; i < target.size(); ++i) {
        char c = target[i];
        if (c == '#') return false; // RFC 9112 §3.2: fragment prohibited in request target
        if (c == '%') {
            if (i + 2 < target.size()) {
                char h1 = target[i + 1];
                char h2 = target[i + 2];
                // Reject %00 (poison NUL byte)
                if (h1 == '0' && h2 == '0') return false;
                // Reject %0D / %0A (CRLF injection)
                if (h1 == '0' && (h2 == 'd' || h2 == 'D' || h2 == 'a' || h2 == 'A')) return false;
            }
        }
    }
    return true;
}

/**
 * @brief Strictly parses and validates decimal Content-Length without overflow
 */
inline std::optional<size_t> parse_valid_content_length(std::string_view value) noexcept {
    if (value.empty() || value.size() > 19) return std::nullopt;
    size_t len = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return std::nullopt;
        size_t next = len * 10 + (c - '0');
        if (next < len) return std::nullopt;
        len = next;
    }
    return len;
}

/**
 * @brief Validates and splits :path header target into path and query components.
 * Returns false if target is invalid per RFC 9112/9113/9114 or asterisk-form is used for non-OPTIONS method.
 * Updates error_status to UriTooLong if target length exceeds MAX_URI_LENGTH.
 */
inline bool parse_path_header(std::string_view target, Method method,
                              std::string_view& path, std::string_view& query,
                              StatusCode& error_status,
                              size_t max_uri_length = DEFAULT_LIMITS.max_uri_length) noexcept {
    if (!is_valid_request_target(target)) {
        return false;
    }
    if (target == "*" && method != Method::OPTIONS && method != Method::UNKNOWN) {
        return false;
    }
    if (target.size() > max_uri_length && error_status == StatusCode::Ok) {
        error_status = StatusCode::UriTooLong;
    }
    size_t qmark = core::simd::SimdString::find_char(target, '?');
    if (qmark != std::string_view::npos) {
        path = target.substr(0, qmark);
        query = target.substr(qmark + 1);
    } else {
        path = target;
        query = {};
    }
    return true;
}

/**
 * @brief Validates common HTTP request headers across protocols (H1, H2, H3).
 * Returns false if a malformed content-length header is present (RFC 9113 §8.2.1 / RFC 9114 §4.2).
 * Flags PayloadTooLarge or ExpectationFailed on error_status if appropriate.
 */
inline bool validate_request_header(std::string_view name, std::string_view value,
                                    StatusCode& error_status,
                                    size_t max_body_size = DEFAULT_LIMITS.max_body_size) noexcept {
    if (core::simd::SimdString::iequals(name, "content-length")) {
        auto cl_opt = parse_valid_content_length(value);
        if (!cl_opt) {
            return false;
        }
        if (*cl_opt > max_body_size && error_status == StatusCode::Ok) {
            error_status = StatusCode::PayloadTooLarge;
        }
    } else if (core::simd::SimdString::iequals(name, "expect")) {
        if (!core::simd::SimdString::iequals(value, "100-continue") && error_status == StatusCode::Ok) {
            error_status = StatusCode::ExpectationFailed;
        }
    }
    return true;
}

} // namespace aegon::http


