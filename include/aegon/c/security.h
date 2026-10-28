#ifndef AEGON_C_SECURITY_H
#define AEGON_C_SECURITY_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/router.h>

AEGON_C_BEGIN

typedef enum aegon_frame_option {
    AEGON_FRAME_DENY       = 0,
    AEGON_FRAME_SAMEORIGIN = 1,
    AEGON_FRAME_DISABLED   = 2
} aegon_frame_option_t;

typedef enum aegon_referrer_policy {
    AEGON_REFERRER_NO_REFERRER                    = 0,
    AEGON_REFERRER_NO_REFERRER_WHEN_DOWNGRADE     = 1,
    AEGON_REFERRER_SAME_ORIGIN                    = 2,
    AEGON_REFERRER_ORIGIN                         = 3,
    AEGON_REFERRER_STRICT_ORIGIN                  = 4,
    AEGON_REFERRER_ORIGIN_WHEN_CROSS_ORIGIN       = 5,
    AEGON_REFERRER_STRICT_ORIGIN_WHEN_CROSS_ORIGIN = 6,  /* default */
    AEGON_REFERRER_UNSAFE_URL                     = 7
} aegon_referrer_policy_t;

typedef enum aegon_coop {
    AEGON_COOP_SAME_ORIGIN = 0,   /* default */
    AEGON_COOP_SAME_ORIGIN_ALLOW_POPUPS = 1,
    AEGON_COOP_UNSAFE_NONE = 2,
    AEGON_COOP_DISABLED    = 3
} aegon_coop_t;

typedef enum aegon_coep {
    AEGON_COEP_REQUIRE_CORP = 0,
    AEGON_COEP_CREDENTIALLESS = 1,
    AEGON_COEP_UNSAFE_NONE  = 2,
    AEGON_COEP_DISABLED     = 3   /* default */
} aegon_coep_t;

typedef enum aegon_corp {
    AEGON_CORP_SAME_ORIGIN  = 0,  /* default */
    AEGON_CORP_SAME_SITE    = 1,
    AEGON_CORP_CROSS_ORIGIN = 2,
    AEGON_CORP_DISABLED     = 3
} aegon_corp_t;

typedef struct aegon_security_config {
    size_t struct_size;
    bool     content_type_nosniff;        /* default true  */
    aegon_frame_option_t frame_options;   /* default DENY  */
    bool     xss_protection_disabled;     /* default true (X-XSS-Protection: 0) */
    aegon_referrer_policy_t referrer_policy;

    /* HSTS */
    bool     hsts_enabled;                /* default true */
    uint32_t hsts_max_age_seconds;        /* default 31536000 */
    bool     hsts_include_subdomains;     /* default true */
    bool     hsts_preload;                /* default false */

    /* Raw policy strings (NUL-terminated, copied; NULL/empty -> header omitted) */
    const char* content_security_policy;  /* default NULL */
    bool        csp_report_only;          /* default false */
    const char* permissions_policy;       /* default NULL */

    aegon_coop_t coop;
    aegon_coep_t coep;
    aegon_corp_t corp;

    bool prevent_browser_caching;         /* default false */
} aegon_security_config_t;

/* Applies SecurityHeadersConfig::defaults(). */
AEGON_C_API void aegon_security_config_init(aegon_security_config_t* cfg);

AEGON_C_API aegon_err_t aegon_router_use_security_headers(aegon_router_t* router,
                                                          const aegon_security_config_t* cfg);
AEGON_C_API aegon_err_t aegon_group_use_security_headers(aegon_group_t* group,
                                                         const aegon_security_config_t* cfg);

AEGON_C_END

#endif /* AEGON_C_SECURITY_H */

