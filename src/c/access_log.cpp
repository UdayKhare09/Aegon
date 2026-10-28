#include <aegon/c/access_log.h>
#include "internal.h"
#include "http/middleware/AccessLog.h"
#include "http/Router.h"
#include "http/RouteGroup.h"
#include <cstring>
#include <chrono>
#include <vector>
#include <string>

extern "C" {

void aegon_access_log_config_init(aegon_access_log_config_t* cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = sizeof(*cfg);
    cfg->format = AEGON_ACCESS_LOG_COMBINED;
    cfg->normal_level = AEGON_LOG_INFO;
    cfg->client_error_level = AEGON_LOG_WARN;
    cfg->server_error_level = AEGON_LOG_ERROR;
    cfg->slow_threshold_ns = 0;
    cfg->slow_level = AEGON_LOG_WARN;
    cfg->log_query = true;
    cfg->log_client_ip = true;
    cfg->log_bytes = true;
    cfg->log_protocol = true;
    cfg->request_headers = nullptr;
    cfg->request_headers_count = 0;
    cfg->response_headers = nullptr;
    cfg->response_headers_count = 0;
    cfg->skip_paths = nullptr;
    cfg->skip_paths_count = 0;
    cfg->skip_if = nullptr;
    cfg->skip_userdata = nullptr;
    cfg->custom_formatter = nullptr;
    cfg->formatter_userdata = nullptr;
    cfg->correlation_header = "x-request-id";
}

static aegon_err_t build_access_log_config(const aegon_access_log_config_t* cfg, aegon::http::middleware::AccessLogConfig& out) {
    if (!cfg) {
        out = aegon::http::middleware::AccessLogConfig{};
        return AEGON_OK;
    }
    if (cfg->struct_size != sizeof(aegon_access_log_config_t)) {
        return AEGON_ERR_INVALID_ARG;
    }

    out.format = static_cast<aegon::http::middleware::AccessLogFormat>(cfg->format);
    out.normal_level = static_cast<aegon::log::Level>(cfg->normal_level);
    out.client_error_level = static_cast<aegon::log::Level>(cfg->client_error_level);
    out.server_error_level = static_cast<aegon::log::Level>(cfg->server_error_level);
    out.slow_threshold = std::chrono::nanoseconds(cfg->slow_threshold_ns);
    out.slow_level = static_cast<aegon::log::Level>(cfg->slow_level);

    out.log_query = cfg->log_query;
    out.log_client_ip = cfg->log_client_ip;
    out.log_bytes = cfg->log_bytes;
    out.log_protocol = cfg->log_protocol;

    if (cfg->request_headers && cfg->request_headers_count > 0) {
        out.request_headers.clear();
        out.request_headers.reserve(cfg->request_headers_count);
        for (size_t i = 0; i < cfg->request_headers_count; ++i) {
            if (cfg->request_headers[i]) out.request_headers.emplace_back(cfg->request_headers[i]);
        }
    }

    if (cfg->response_headers && cfg->response_headers_count > 0) {
        out.response_headers.clear();
        out.response_headers.reserve(cfg->response_headers_count);
        for (size_t i = 0; i < cfg->response_headers_count; ++i) {
            if (cfg->response_headers[i]) out.response_headers.emplace_back(cfg->response_headers[i]);
        }
    }

    if (cfg->skip_paths && cfg->skip_paths_count > 0) {
        out.skip_paths.clear();
        out.skip_paths.reserve(cfg->skip_paths_count);
        for (size_t i = 0; i < cfg->skip_paths_count; ++i) {
            if (cfg->skip_paths[i]) out.skip_paths.emplace_back(cfg->skip_paths[i]);
        }
    }

    if (cfg->skip_if) {
        auto skip_fn = cfg->skip_if;
        auto* ud = cfg->skip_userdata;
        out.skip_if = [skip_fn, ud](const aegon::http::Context& ctx) -> bool {
            return skip_fn(aegon::c::to_c(const_cast<aegon::http::Context*>(&ctx)), ud);
        };
    }

    if (cfg->custom_formatter) {
        auto fmt_fn = cfg->custom_formatter;
        auto* ud = cfg->formatter_userdata;
        out.custom_formatter = [fmt_fn, ud](const aegon::http::Context& ctx, std::chrono::nanoseconds duration) -> std::string {
            char buf[2048];
            size_t n = fmt_fn(aegon::c::to_c(const_cast<aegon::http::Context*>(&ctx)),
                              static_cast<uint64_t>(duration.count()),
                              buf, sizeof(buf), ud);
            if (n == 0) return "";
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            return std::string(buf, n);
        };
    }

    if (cfg->correlation_header) {
        out.correlation_header = cfg->correlation_header;
    } else {
        out.correlation_header.clear();
    }

    return AEGON_OK;
}

aegon_err_t aegon_router_use_access_log(aegon_router_t* router, const aegon_access_log_config_t* cfg) {
    if (!router) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::middleware::AccessLogConfig cpp_cfg;
        aegon_err_t err = build_access_log_config(cfg, cpp_cfg);
        if (err != AEGON_OK) return err;
        aegon::c::to_cpp(router)->use(aegon::http::middleware::access_log(std::move(cpp_cfg)));
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_use_access_log(aegon_group_t* group, const aegon_access_log_config_t* cfg) {
    if (!group) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::middleware::AccessLogConfig cpp_cfg;
        aegon_err_t err = build_access_log_config(cfg, cpp_cfg);
        if (err != AEGON_OK) return err;
        aegon::c::to_group_handle(group)->grp.use(aegon::http::middleware::access_log(std::move(cpp_cfg)));
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"

