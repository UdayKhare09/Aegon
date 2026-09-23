#pragma once

#include <cstdint>
#include <string_view>

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
    UnprocessableEntity = 422,
    UpgradeRequired = 426,
    TooManyRequests = 429,

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
        case StatusCode::UnprocessableEntity: return "Unprocessable Entity";
        case StatusCode::UpgradeRequired: return "Upgrade Required";
        case StatusCode::TooManyRequests: return "Too Many Requests";
        case StatusCode::InternalServerError: return "Internal Server Error";
        case StatusCode::NotImplemented: return "Not Implemented";
        case StatusCode::BadGateway: return "Bad Gateway";
        case StatusCode::ServiceUnavailable: return "Service Unavailable";
        case StatusCode::GatewayTimeout: return "Gateway Timeout";
        case StatusCode::HttpVersionNotSupported: return "HTTP Version Not Supported";
    }
    return "Unknown";
}

} // namespace aegon::http
