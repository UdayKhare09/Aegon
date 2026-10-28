#include <aegon/c/compress.h>
#include "internal.h"
#include "http/middleware/Compress.h"
#include "http/Router.h"
#include "http/RouteGroup.h"
#include <cstring>

extern "C" {

void aegon_compress_config_init(aegon_compress_config_t* cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = sizeof(*cfg);
    cfg->level = 1;
    cfg->min_size = 256;
    cfg->gzip = true;
    cfg->deflate = true;
    cfg->prefer_deflate = true;
    cfg->vary = true;
}

static aegon_err_t build_compress_options(const aegon_compress_config_t* cfg, aegon::http::middleware::CompressOptions& out) {
    if (!cfg) {
        // default CompressOptions
        out = aegon::http::middleware::CompressOptions{};
        return AEGON_OK;
    }
    if (cfg->struct_size != sizeof(aegon_compress_config_t)) {
        return AEGON_ERR_INVALID_ARG;
    }

    out.level = (cfg->level >= 1 && cfg->level <= 12) ? cfg->level : 1;
    out.min_size = cfg->min_size;
    out.gzip = cfg->gzip;
    out.deflate = cfg->deflate;
    out.prefer_deflate = cfg->prefer_deflate;
    out.vary = cfg->vary;

    return AEGON_OK;
}

aegon_err_t aegon_router_use_compress(aegon_router_t* router, const aegon_compress_config_t* cfg) {
    if (!router) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::middleware::CompressOptions opts;
        aegon_err_t err = build_compress_options(cfg, opts);
        if (err != AEGON_OK) return err;
        aegon::c::to_cpp(router)->use(aegon::http::middleware::Compress(opts));
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_use_compress(aegon_group_t* group, const aegon_compress_config_t* cfg) {
    if (!group) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::middleware::CompressOptions opts;
        aegon_err_t err = build_compress_options(cfg, opts);
        if (err != AEGON_OK) return err;
        aegon::c::to_group_handle(group)->grp.use(aegon::http::middleware::Compress(opts));
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"

