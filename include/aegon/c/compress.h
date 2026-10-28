#ifndef AEGON_C_COMPRESS_H
#define AEGON_C_COMPRESS_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

typedef struct aegon_compress_config {
    size_t struct_size;
    int    level;           /* 1 (fastest, default) .. 12 (max) — libdeflate */
    size_t min_size;        /* skip bodies smaller than this; default 256 */
    bool   gzip;            /* default true */
    bool   deflate;         /* default true */
    bool   prefer_deflate;  /* default true */
    bool   vary;            /* merge Vary: Accept-Encoding; default true */
} aegon_compress_config_t;

AEGON_C_API void aegon_compress_config_init(aegon_compress_config_t* cfg);

AEGON_C_API aegon_err_t aegon_router_use_compress(aegon_router_t* router,
                                                  const aegon_compress_config_t* cfg);
AEGON_C_API aegon_err_t aegon_group_use_compress(aegon_group_t* group,
                                                 const aegon_compress_config_t* cfg);

AEGON_C_END

#endif /* AEGON_C_COMPRESS_H */

