#include "Version.h"
#include "internal.h"
#include <aegon/c/aegon.h>
#include "http/Protocol.h"
#include <cstring>
#include <cstdlib>

namespace aegon::c {

static thread_local std::string tls_last_error;

void set_last_error(std::string_view msg) noexcept {
    try {
        tls_last_error.assign(msg.data(), msg.size());
    } catch (...) {
        // Fallback on memory allocation failure
    }
}

const char* get_last_error() noexcept {
    return tls_last_error.c_str();
}

void clear_last_error() noexcept {
    tls_last_error.clear();
}

} // namespace aegon::c

extern "C" {

const char* aegon_last_error(void) {
    return aegon::c::get_last_error();
}

void aegon_last_error_clear(void) {
    aegon::c::clear_last_error();
}

const char* aegon_err_str(aegon_err_t err) {
    switch (err) {
        case AEGON_OK:                   return "AEGON_OK";
        case AEGON_ERR_INVALID_ARG:      return "AEGON_ERR_INVALID_ARG";
        case AEGON_ERR_NOMEM:            return "AEGON_ERR_NOMEM";
        case AEGON_ERR_IO:               return "AEGON_ERR_IO";
        case AEGON_ERR_TLS:              return "AEGON_ERR_TLS";
        case AEGON_ERR_NOT_FOUND:        return "AEGON_ERR_NOT_FOUND";
        case AEGON_ERR_STATE:            return "AEGON_ERR_STATE";
        case AEGON_ERR_UNSUPPORTED:      return "AEGON_ERR_UNSUPPORTED";
        case AEGON_ERR_BUFFER_TOO_SMALL: return "AEGON_ERR_BUFFER_TOO_SMALL";
        case AEGON_ERR_INTERNAL:         return "AEGON_ERR_INTERNAL";
        default:                         return "AEGON_ERR_UNKNOWN";
    }
}

uint32_t aegon_version(void) {
    return AEGON_VERSION_HEX;
}

const char* aegon_version_string(void) {
    return aegon::version.string.data();
}

uint32_t aegon_abi_version(void) {
    return AEGON_ABI_VERSION;
}

aegon_str_t aegon_str(const char* cstr) {
    if (!cstr) {
        return aegon_str_t{nullptr, 0};
    }
    return aegon_str_t{cstr, std::strlen(cstr)};
}

aegon_str_t aegon_str_dup(aegon_str_t s) {
    if (!s.data && s.len == 0) {
        return aegon_str_t{nullptr, 0};
    }
    char* buf = static_cast<char*>(std::malloc(s.len + 1));
    if (!buf) {
        return aegon_str_t{nullptr, 0};
    }
    if (s.len > 0 && s.data) {
        std::memcpy(buf, s.data, s.len);
    }
    buf[s.len] = '\0';
    return aegon_str_t{buf, s.len};
}

void aegon_mem_free(void* p) {
    std::free(p);
}

const char* aegon_status_phrase(uint16_t status) {
    return aegon::http::status_phrase(static_cast<aegon::http::StatusCode>(status)).data();
}

const char* aegon_method_cstr(aegon_method_t m) {
    switch (m) {
        case AEGON_METHOD_GET:     return "GET";
        case AEGON_METHOD_POST:    return "POST";
        case AEGON_METHOD_PUT:     return "PUT";
        case AEGON_METHOD_DELETE:  return "DELETE";
        case AEGON_METHOD_PATCH:   return "PATCH";
        case AEGON_METHOD_HEAD:    return "HEAD";
        case AEGON_METHOD_OPTIONS: return "OPTIONS";
        case AEGON_METHOD_CONNECT: return "CONNECT";
        case AEGON_METHOD_TRACE:   return "TRACE";
        case AEGON_METHOD_UNKNOWN:
        default:                   return "UNKNOWN";
    }
}

aegon_method_t aegon_method_from_str(aegon_str_t s) {
    if (!s.data && s.len == 0) {
        return AEGON_METHOD_UNKNOWN;
    }
    std::string_view sv = aegon::c::to_sv(s);
    auto m = aegon::http::string_to_method(sv);
    switch (m) {
        case aegon::http::Method::GET:     return AEGON_METHOD_GET;
        case aegon::http::Method::POST:    return AEGON_METHOD_POST;
        case aegon::http::Method::PUT:     return AEGON_METHOD_PUT;
        case aegon::http::Method::DELETE:  return AEGON_METHOD_DELETE;
        case aegon::http::Method::PATCH:   return AEGON_METHOD_PATCH;
        case aegon::http::Method::HEAD:    return AEGON_METHOD_HEAD;
        case aegon::http::Method::OPTIONS: return AEGON_METHOD_OPTIONS;
        case aegon::http::Method::CONNECT: return AEGON_METHOD_CONNECT;
        case aegon::http::Method::TRACE:   return AEGON_METHOD_TRACE;
        default:                           return AEGON_METHOD_UNKNOWN;
    }
}

const char* aegon_http_version_cstr(aegon_http_version_t v) {
    switch (v) {
        case AEGON_HTTP_1_0: return "HTTP/1.0";
        case AEGON_HTTP_1_1: return "HTTP/1.1";
        case AEGON_HTTP_2:   return "HTTP/2.0";
        case AEGON_HTTP_3:   return "HTTP/3.0";
        default:             return "HTTP/UNKNOWN";
    }
}

} // extern "C"
