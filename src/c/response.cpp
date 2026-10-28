#include "internal.h"
#include <aegon/c/response.h>
#include "http/Response.h"
#include <cstring>

extern "C" {

void aegon_res_status(aegon_res_t* res, uint16_t status) {
    if (!res) return;
    aegon::c::to_cpp(res)->status(status);
}

aegon_err_t aegon_res_header(aegon_res_t* res, aegon_str_t name, aegon_str_t value) {
    AEGON_C_TRY {
        if (!res || !name.data) return AEGON_ERR_INVALID_ARG;
        std::string n(aegon::c::to_sv(name));
        std::string v(aegon::c::to_sv(value));
        aegon::c::to_cpp(res)->set_header_owned(std::move(n), std::move(v));
        return AEGON_OK;
    } AEGON_C_CATCH;
}

void aegon_res_header_static(aegon_res_t* res, aegon_str_t name, aegon_str_t value) {
    if (!res || !name.data) return;
    aegon::c::to_cpp(res)->header(aegon::c::to_sv(name), aegon::c::to_sv(value));
}

aegon_err_t aegon_res_body(aegon_res_t* res, aegon_str_t data) {
    AEGON_C_TRY {
        if (!res) return AEGON_ERR_INVALID_ARG;
        std::string b(aegon::c::to_sv(data));
        aegon::c::to_cpp(res)->body(std::move(b));
        return AEGON_OK;
    } AEGON_C_CATCH;
}

void aegon_res_body_static(aegon_res_t* res, aegon_str_t data) {
    if (!res) return;
    aegon::c::to_cpp(res)->body_view(aegon::c::to_sv(data));
}

aegon_err_t aegon_res_text(aegon_res_t* res, aegon_str_t text) {
    AEGON_C_TRY {
        if (!res) return AEGON_ERR_INVALID_ARG;
        aegon::c::to_cpp(res)->text(aegon::c::to_sv(text));
        return AEGON_OK;
    } AEGON_C_CATCH;
}

aegon_err_t aegon_res_json(aegon_res_t* res, aegon_str_t json) {
    AEGON_C_TRY {
        if (!res) return AEGON_ERR_INVALID_ARG;
        aegon::c::to_cpp(res)->json(aegon::c::to_sv(json));
        return AEGON_OK;
    } AEGON_C_CATCH;
}

aegon_err_t aegon_res_html(aegon_res_t* res, aegon_str_t html) {
    AEGON_C_TRY {
        if (!res) return AEGON_ERR_INVALID_ARG;
        aegon::c::to_cpp(res)->html(aegon::c::to_sv(html));
        return AEGON_OK;
    } AEGON_C_CATCH;
}

void aegon_cookie_options_init(aegon_cookie_options_t* opts) {
    if (!opts) return;
    std::memset(opts, 0, sizeof(*opts));
    opts->struct_size = sizeof(aegon_cookie_options_t);
    opts->path = aegon_str_t{"/", 1};
    opts->max_age_seconds = -1;
    opts->same_site = AEGON_SAMESITE_LAX;
    opts->http_only = false;
    opts->secure = false;
    opts->partitioned = false;
}

aegon_err_t aegon_res_set_cookie(aegon_res_t* res, const aegon_cookie_options_t* opts) {
    AEGON_C_TRY {
        if (!res || !opts || !opts->name.data) return AEGON_ERR_INVALID_ARG;
        aegon::http::CookieOptions cpp_opts;
        cpp_opts.name = aegon::c::to_sv(opts->name);
        cpp_opts.value = aegon::c::to_sv(opts->value);
        cpp_opts.path = opts->path.data ? aegon::c::to_sv(opts->path) : "";
        cpp_opts.domain = opts->domain.data ? aegon::c::to_sv(opts->domain) : "";
        if (opts->max_age_seconds >= 0) {
            cpp_opts.max_age = std::chrono::seconds(opts->max_age_seconds);
        } else {
            cpp_opts.max_age = std::nullopt;
        }
        switch (opts->same_site) {
            case AEGON_SAMESITE_DEFAULT: cpp_opts.same_site = aegon::http::SameSite::Default; break;
            case AEGON_SAMESITE_LAX:     cpp_opts.same_site = aegon::http::SameSite::Lax; break;
            case AEGON_SAMESITE_STRICT:  cpp_opts.same_site = aegon::http::SameSite::Strict; break;
            case AEGON_SAMESITE_NONE:    cpp_opts.same_site = aegon::http::SameSite::None; break;
        }
        cpp_opts.http_only = opts->http_only;
        cpp_opts.secure = opts->secure;
        cpp_opts.partitioned = opts->partitioned;
        aegon::c::to_cpp(res)->set_cookie(cpp_opts);
        return AEGON_OK;
    } AEGON_C_CATCH;
}

aegon_err_t aegon_res_clear_cookie(aegon_res_t* res, aegon_str_t name, aegon_str_t path) {
    AEGON_C_TRY {
        if (!res || !name.data) return AEGON_ERR_INVALID_ARG;
        std::string_view p = path.data ? aegon::c::to_sv(path) : "/";
        aegon::c::to_cpp(res)->clear_cookie(aegon::c::to_sv(name), p);
        return AEGON_OK;
    } AEGON_C_CATCH;
}

aegon_err_t aegon_res_file(aegon_res_t* res, const char* path, aegon_str_t mime) {
    AEGON_C_TRY {
        if (!res || !path) return AEGON_ERR_INVALID_ARG;
        std::string_view m = mime.data ? aegon::c::to_sv(mime) : "";
        aegon::c::to_cpp(res)->file(path, m);
        return AEGON_OK;
    } AEGON_C_CATCH;
}

aegon_err_t aegon_res_redirect(aegon_res_t* res, uint16_t status, aegon_str_t location) {
    AEGON_C_TRY {
        if (!res || !location.data) return AEGON_ERR_INVALID_ARG;
        if (status < 300 || status > 399) {
            return aegon::c::record_error("redirect status must be 3xx", AEGON_ERR_INVALID_ARG);
        }
        auto* r = aegon::c::to_cpp(res);
        r->status(status);
        r->set_header_owned("Location", std::string(aegon::c::to_sv(location)));
        return AEGON_OK;
    } AEGON_C_CATCH;
}

const char* aegon_mime_from_extension(const char* path) {
    if (!path) return "application/octet-stream";
    std::string_view sv(path);
    if (sv.rfind('.') == std::string_view::npos) {
        std::string with_dot = "." + std::string(sv);
        return aegon::http::Response::infer_mime_type(with_dot).data();
    }
    return aegon::http::Response::infer_mime_type(sv).data();
}

aegon_err_t aegon_res_trailer(aegon_res_t* res, aegon_str_t name, aegon_str_t value) {
    AEGON_C_TRY {
        if (!res || !name.data) return AEGON_ERR_INVALID_ARG;
        std::string n(aegon::c::to_sv(name));
        std::string v(aegon::c::to_sv(value));
        aegon::c::to_cpp(res)->set_trailer_owned(std::move(n), std::move(v));
        return AEGON_OK;
    } AEGON_C_CATCH;
}

void aegon_res_trailer_static(aegon_res_t* res, aegon_str_t name, aegon_str_t value) {
    if (!res || !name.data) return;
    aegon::c::to_cpp(res)->trailer(aegon::c::to_sv(name), aegon::c::to_sv(value));
}

void aegon_res_trailers_only(aegon_res_t* res, bool on) {
    if (!res) return;
    aegon::c::to_cpp(res)->trailers_only(on);
}

} // extern "C"
