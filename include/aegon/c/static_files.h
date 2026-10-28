#ifndef AEGON_C_STATIC_FILES_H
#define AEGON_C_STATIC_FILES_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

/* Optional Cache-Control policy callback (StaticFilesOptions::cache_control).
 * path is the URL path requested; is_fallback is true when serving the SPA
 * fallback document. Write into buf (NUL-terminate), return byte count; return 0
 * to omit the header. */
typedef size_t (*aegon_cache_control_fn)(aegon_str_t path, bool is_fallback,
                                         char* buf, size_t buf_cap, void* userdata);

typedef struct aegon_static_files_config {
    size_t struct_size;
    bool precompressed;        /* serve .br/.gz sidecars; default true */
    bool cache_in_memory;      /* mtime-revalidated in-memory cache; default true */
    const char* index_file;    /* default "index.html"; copied */
    bool spa;                  /* HTML5 history fallback; default false */
    const char* fallback_file; /* default "index.html"; copied */
    bool etag;                 /* default true */
    bool last_modified;        /* default true */
    aegon_cache_control_fn cache_control;   /* NULL -> framework defaults */
    void*                  cache_control_userdata;
} aegon_static_files_config_t;

AEGON_C_API void aegon_static_files_config_init(aegon_static_files_config_t* cfg);

/* Serves directory under URL prefix. AEGON_ERR_INVALID_ARG if either path is
 * empty; AEGON_ERR_IO if the directory doesn't exist at registration time. */
AEGON_C_API aegon_err_t aegon_router_static_files(aegon_router_t* router,
                                                  const char* prefix,
                                                  const char* directory,
                                                  const aegon_static_files_config_t* cfg);
AEGON_C_API aegon_err_t aegon_group_static_files(aegon_group_t* group,
                                                 const char* prefix,
                                                 const char* directory,
                                                 const aegon_static_files_config_t* cfg);

AEGON_C_END

#endif /* AEGON_C_STATIC_FILES_H */

