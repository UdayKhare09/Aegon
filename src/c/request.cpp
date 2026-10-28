#include "internal.h"
#include <aegon/c/request.h>
#include "http/Request.h"
#include <cstdlib>

extern "C" {

aegon_method_t aegon_req_method(aegon_req_t* req) {
    if (!req) return AEGON_METHOD_UNKNOWN;
    auto* r = aegon::c::to_cpp(req);
    switch (r->method()) {
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

aegon_http_version_t aegon_req_version(aegon_req_t* req) {
    if (!req) return AEGON_HTTP_1_1;
    auto* r = aegon::c::to_cpp(req);
    switch (r->version()) {
        case aegon::http::HttpVersion::Http1_0: return AEGON_HTTP_1_0;
        case aegon::http::HttpVersion::Http1_1: return AEGON_HTTP_1_1;
        case aegon::http::HttpVersion::Http2:   return AEGON_HTTP_2;
        case aegon::http::HttpVersion::Http3:   return AEGON_HTTP_3;
        default:                                return AEGON_HTTP_1_1;
    }
}

aegon_str_t aegon_req_path(aegon_req_t* req) {
    if (!req) return aegon_str_t{nullptr, 0};
    return aegon::c::to_c_str(aegon::c::to_cpp(req)->path());
}

aegon_str_t aegon_req_query_string(aegon_req_t* req) {
    if (!req) return aegon_str_t{nullptr, 0};
    return aegon::c::to_c_str(aegon::c::to_cpp(req)->query());
}

aegon_str_t aegon_req_body(aegon_req_t* req) {
    if (!req) return aegon_str_t{nullptr, 0};
    return aegon::c::to_c_str(aegon::c::to_cpp(req)->body());
}

aegon_str_t aegon_req_header(aegon_req_t* req, aegon_str_t name) {
    if (!req) return aegon_str_t{nullptr, 0};
    auto opt = aegon::c::to_cpp(req)->headers().get(aegon::c::to_sv(name));
    return opt ? aegon::c::to_c_str(*opt) : aegon_str_t{nullptr, 0};
}

bool aegon_req_has_header(aegon_req_t* req, aegon_str_t name) {
    if (!req) return false;
    return aegon::c::to_cpp(req)->headers().contains(aegon::c::to_sv(name));
}

size_t aegon_req_header_count(aegon_req_t* req) {
    if (!req) return 0;
    return aegon::c::to_cpp(req)->headers().size();
}

bool aegon_req_header_at(aegon_req_t* req, size_t index,
                         aegon_str_t* out_name,
                         aegon_str_t* out_value) {
    if (!req) return false;
    const auto& hdrs = aegon::c::to_cpp(req)->headers();
    if (index >= hdrs.size()) return false;
    auto it = hdrs.begin();
    for (size_t i = 0; i < index; ++i) {
        ++it;
    }
    const auto& entry = *it;
    if (out_name) *out_name = aegon::c::to_c_str(entry.name);
    if (out_value) *out_value = aegon::c::to_c_str(entry.value);
    return true;
}

aegon_str_t aegon_req_param(aegon_req_t* req, aegon_str_t key) {
    if (!req) return aegon_str_t{nullptr, 0};
    auto opt = aegon::c::to_cpp(req)->param(aegon::c::to_sv(key));
    return opt ? aegon::c::to_c_str(*opt) : aegon_str_t{nullptr, 0};
}

size_t aegon_req_param_count(aegon_req_t* req) {
    if (!req) return 0;
    return aegon::c::to_cpp(req)->param_count();
}

bool aegon_req_param_at(aegon_req_t* req, size_t index,
                        aegon_str_t* out_key,
                        aegon_str_t* out_value) {
    if (!req) return false;
    auto params = aegon::c::to_cpp(req)->params();
    if (index >= params.size()) return false;
    if (out_key) *out_key = aegon::c::to_c_str(params[index].key);
    if (out_value) *out_value = aegon::c::to_c_str(params[index].value);
    return true;
}

aegon_str_t aegon_req_query(aegon_req_t* req, aegon_str_t key) {
    if (!req) return aegon_str_t{nullptr, 0};
    auto opt = aegon::c::to_cpp(req)->query_param(aegon::c::to_sv(key));
    return opt ? aegon::c::to_c_str(*opt) : aegon_str_t{nullptr, 0};
}

aegon_str_t aegon_req_cookie(aegon_req_t* req, aegon_str_t name) {
    if (!req) return aegon_str_t{nullptr, 0};
    auto opt = aegon::c::to_cpp(req)->cookie(aegon::c::to_sv(name));
    return opt ? aegon::c::to_c_str(*opt) : aegon_str_t{nullptr, 0};
}

bool aegon_req_is_grpc(aegon_req_t* req) {
    if (!req) return false;
    return aegon::c::to_cpp(req)->is_grpc();
}

bool aegon_req_is_websocket(aegon_req_t* req) {
    if (!req) return false;
    auto opt = aegon::c::to_cpp(req)->headers().get("upgrade");
    return opt && aegon::http::iequals(*opt, "websocket");
}

aegon_str_t aegon_url_decode(aegon_str_t in) {
    if (!in.data && in.len == 0) {
        return aegon_str_t{nullptr, 0};
    }
    std::string decoded = aegon::http::Request::url_decode(aegon::c::to_sv(in));
    return aegon_str_dup(aegon::c::to_c_str(decoded));
}

} // extern "C"

