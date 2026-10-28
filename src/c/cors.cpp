#include <aegon/c/cors.h>
#include "internal.h"
#include "http/middleware/Cors.h"
#include "http/Router.h"
#include "http/RouteGroup.h"
#include <cstring>
#include <vector>
#include <string>
#include <chrono>

extern "C" {

void aegon_cors_config_init(aegon_cors_config_t* cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = sizeof(*cfg);
    cfg->allowed_origins = nullptr;
    cfg->allowed_origins_count = 0;
    cfg->origin_patterns = nullptr;
    cfg->origin_patterns_count = 0;
    cfg->allowed_methods = AEGON_METHODS_STANDARD;
    cfg->allowed_headers = nullptr;
    cfg->allowed_headers_count = 0;
    cfg->expose_headers = nullptr;
    cfg->expose_headers_count = 0;
    cfg->allow_credentials = false;
    cfg->allow_private_network = false;
    cfg->max_age_seconds = 86400;
    cfg->vary_header = true;
}

static aegon_err_t build_cors_config(const aegon_cors_config_t* cfg, aegon::http::middleware::CorsConfig& out) {
    if (!cfg) {
        out = aegon::http::middleware::CorsConfig::permissive();
        return AEGON_OK;
    }
    if (cfg->struct_size != sizeof(aegon_cors_config_t)) {
        return AEGON_ERR_INVALID_ARG;
    }

    if (cfg->allowed_origins && cfg->allowed_origins_count > 0) {
        out.allowed_origins.clear();
        out.allowed_origins.reserve(cfg->allowed_origins_count);
        for (size_t i = 0; i < cfg->allowed_origins_count; ++i) {
            if (cfg->allowed_origins[i]) {
                out.allowed_origins.emplace_back(cfg->allowed_origins[i]);
            }
        }
    } else {
        out.allowed_origins.clear();
    }

    if (cfg->origin_patterns && cfg->origin_patterns_count > 0) {
        out.origin_patterns.reserve(cfg->origin_patterns_count);
        for (size_t i = 0; i < cfg->origin_patterns_count; ++i) {
            if (cfg->origin_patterns[i]) {
                out.origin_patterns.emplace_back(cfg->origin_patterns[i]);
            }
        }
    }

    if (cfg->allowed_methods != 0) {
        out.allowed_methods.clear();
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_GET)) out.allowed_methods.push_back(aegon::http::Method::GET);
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_POST)) out.allowed_methods.push_back(aegon::http::Method::POST);
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_PUT)) out.allowed_methods.push_back(aegon::http::Method::PUT);
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_DELETE)) out.allowed_methods.push_back(aegon::http::Method::DELETE);
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_PATCH)) out.allowed_methods.push_back(aegon::http::Method::PATCH);
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_HEAD)) out.allowed_methods.push_back(aegon::http::Method::HEAD);
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_OPTIONS)) out.allowed_methods.push_back(aegon::http::Method::OPTIONS);
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_CONNECT)) out.allowed_methods.push_back(aegon::http::Method::CONNECT);
        if (cfg->allowed_methods & AEGON_METHOD_BIT(AEGON_METHOD_TRACE)) out.allowed_methods.push_back(aegon::http::Method::TRACE);
    }

    if (cfg->allowed_headers && cfg->allowed_headers_count > 0) {
        out.allowed_headers.clear();
        out.allowed_headers.reserve(cfg->allowed_headers_count);
        for (size_t i = 0; i < cfg->allowed_headers_count; ++i) {
            if (cfg->allowed_headers[i]) {
                out.allowed_headers.emplace_back(cfg->allowed_headers[i]);
            }
        }
    } else {
        out.allowed_headers = {"*"};
    }

    if (cfg->expose_headers && cfg->expose_headers_count > 0) {
        out.expose_headers.clear();
        out.expose_headers.reserve(cfg->expose_headers_count);
        for (size_t i = 0; i < cfg->expose_headers_count; ++i) {
            if (cfg->expose_headers[i]) {
                out.expose_headers.emplace_back(cfg->expose_headers[i]);
            }
        }
    }

    out.allow_credentials = cfg->allow_credentials;
    out.allow_private_network = cfg->allow_private_network;
    out.max_age = std::chrono::seconds(cfg->max_age_seconds);
    out.vary_header = cfg->vary_header;

    return AEGON_OK;
}

aegon_err_t aegon_router_use_cors(aegon_router_t* router, const aegon_cors_config_t* cfg) {
    if (!router) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::middleware::CorsConfig cpp_cfg;
        aegon_err_t err = build_cors_config(cfg, cpp_cfg);
        if (err != AEGON_OK) return err;
        aegon::c::to_cpp(router)->use(aegon::http::middleware::cors(std::move(cpp_cfg)));
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_use_cors(aegon_group_t* group, const aegon_cors_config_t* cfg) {
    if (!group) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::middleware::CorsConfig cpp_cfg;
        aegon_err_t err = build_cors_config(cfg, cpp_cfg);
        if (err != AEGON_OK) return err;
        aegon::c::to_group_handle(group)->grp.use(aegon::http::middleware::cors(std::move(cpp_cfg)));
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"

