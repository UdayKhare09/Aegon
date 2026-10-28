#include <aegon/c/static_files.h>
#include "internal.h"
#include "http/Router.h"
#include "http/RouteGroup.h"
#include <cstring>
#include <filesystem>

extern "C" {

void aegon_static_files_config_init(aegon_static_files_config_t* cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = sizeof(*cfg);
    cfg->precompressed = true;
    cfg->cache_in_memory = true;
    cfg->index_file = "index.html";
    cfg->spa = false;
    cfg->fallback_file = "index.html";
    cfg->etag = true;
    cfg->last_modified = true;
    cfg->cache_control = nullptr;
    cfg->cache_control_userdata = nullptr;
}

static aegon_err_t build_static_options(const aegon_static_files_config_t* cfg,
                                        const char* directory,
                                        aegon::http::StaticFilesOptions& out) {
    if (!directory || *directory == '\0') {
        return AEGON_ERR_INVALID_ARG;
    }

    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec)) {
        return AEGON_ERR_IO;
    }

    if (!cfg) {
        out = aegon::http::StaticFilesOptions{};
        return AEGON_OK;
    }

    if (cfg->struct_size != sizeof(aegon_static_files_config_t)) {
        return AEGON_ERR_INVALID_ARG;
    }

    out.precompressed = cfg->precompressed;
    out.cache_in_memory = cfg->cache_in_memory;
    out.index_file = cfg->index_file ? cfg->index_file : "index.html";
    out.spa = cfg->spa;
    out.fallback_file = cfg->fallback_file ? cfg->fallback_file : "index.html";
    out.etag = cfg->etag;
    out.last_modified = cfg->last_modified;

    if (cfg->cache_control) {
        auto cb = cfg->cache_control;
        auto* ud = cfg->cache_control_userdata;
        out.cache_control = [cb, ud](std::string_view path, bool is_fallback) -> std::string {
            char buf[512];
            aegon_str_t p{path.data(), path.size()};
            size_t n = cb(p, is_fallback, buf, sizeof(buf), ud);
            if (n == 0) return "";
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            return std::string(buf, n);
        };
    }

    return AEGON_OK;
}

aegon_err_t aegon_router_static_files(aegon_router_t* router,
                                      const char* prefix,
                                      const char* directory,
                                      const aegon_static_files_config_t* cfg) {
    if (!router || !prefix || !directory) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::StaticFilesOptions opts;
        aegon_err_t err = build_static_options(cfg, directory, opts);
        if (err != AEGON_OK) return err;

        aegon::c::to_cpp(router)->static_files(prefix, directory, std::move(opts));
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_static_files(aegon_group_t* group,
                                     const char* prefix,
                                     const char* directory,
                                     const aegon_static_files_config_t* cfg) {
    if (!group || !prefix || !directory) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::http::StaticFilesOptions opts;
        aegon_err_t err = build_static_options(cfg, directory, opts);
        if (err != AEGON_OK) return err;

        aegon::c::to_group_handle(group)->grp.static_files(prefix, directory, std::move(opts));
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"

