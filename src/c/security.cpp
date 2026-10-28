#include <aegon/c/security.h>
#include "internal.h"
#include "http/middleware/SecurityHeaders.h"
#include "http/Router.h"
#include "http/RouteGroup.h"
#include <cstring>
#include <chrono>

extern "C" {

void aegon_security_config_init(aegon_security_config_t* cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = sizeof(*cfg);
    cfg->content_type_nosniff = true;
    cfg->frame_options = AEGON_FRAME_DENY;
    cfg->xss_protection_disabled = true;
    cfg->referrer_policy = AEGON_REFERRER_STRICT_ORIGIN_WHEN_CROSS_ORIGIN;
    cfg->hsts_enabled = true;
    cfg->hsts_max_age_seconds = 31536000;
    cfg->hsts_include_subdomains = true;
    cfg->hsts_preload = false;
    cfg->content_security_policy = nullptr;
    cfg->csp_report_only = false;
    cfg->permissions_policy = nullptr;
    cfg->coop = AEGON_COOP_SAME_ORIGIN;
    cfg->coep = AEGON_COEP_DISABLED;
    cfg->corp = AEGON_CORP_SAME_ORIGIN;
    cfg->prevent_browser_caching = false;
}

static aegon_err_t build_security_config(const aegon_security_config_t* cfg, aegon::http::middleware::SecurityHeadersConfig& out) {
    if (!cfg) {
        out = aegon::http::middleware::SecurityHeadersConfig::defaults();
        return AEGON_OK;
    }
    if (cfg->struct_size != sizeof(aegon_security_config_t)) {
        return AEGON_ERR_INVALID_ARG;
    }

    out.content_type_nosniff = cfg->content_type_nosniff;
    out.frame_options = static_cast<aegon::http::middleware::FrameOption>(cfg->frame_options);
    out.xss_protection_disabled = cfg->xss_protection_disabled;
    out.referrer_policy = static_cast<aegon::http::middleware::ReferrerPolicy>(cfg->referrer_policy);

    out.hsts.enabled = cfg->hsts_enabled;
    out.hsts.max_age = std::chrono::seconds(cfg->hsts_max_age_seconds);
    out.hsts.include_subdomains = cfg->hsts_include_subdomains;
    out.hsts.preload = cfg->hsts_preload;

    if (cfg->content_security_policy) {
        out.content_security_policy = cfg->content_security_policy;
    } else {
        out.content_security_policy.clear();
    }
    out.csp_report_only = cfg->csp_report_only;

    if (cfg->permissions_policy) {
        out.permissions_policy = cfg->permissions_policy;
    } else {
        out.permissions_policy.clear();
    }

    out.coop = static_cast<aegon::http::middleware::CrossOriginOpenerPolicy>(cfg->coop);
    out.coep = static_cast<aegon::http::middleware::CrossOriginEmbedderPolicy>(cfg->coep);
    out.corp = static_cast<aegon::http::middleware::CrossOriginResourcePolicy>(cfg->corp);

    out.prevent_browser_caching = cfg->prevent_browser_caching;

    return AEGON_OK;
}

aegon_err_t aegon_router_use_security_headers(aegon_router_t* router, const aegon_security_config_t* cfg) {
    if (!router) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::middleware::SecurityHeadersConfig cpp_cfg;
        aegon_err_t err = build_security_config(cfg, cpp_cfg);
        if (err != AEGON_OK) return err;
        aegon::c::to_cpp(router)->use(aegon::http::middleware::security_headers(std::move(cpp_cfg)));
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_use_security_headers(aegon_group_t* group, const aegon_security_config_t* cfg) {
    if (!group) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::middleware::SecurityHeadersConfig cpp_cfg;
        aegon_err_t err = build_security_config(cfg, cpp_cfg);
        if (err != AEGON_OK) return err;
        aegon::c::to_group_handle(group)->grp.use(aegon::http::middleware::security_headers(std::move(cpp_cfg)));
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"

