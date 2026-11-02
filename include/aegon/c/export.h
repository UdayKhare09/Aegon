#ifndef AEGON_C_EXPORT_H
#define AEGON_C_EXPORT_H

/* Symbol visibility. Define AEGON_C_STATIC when linking libaegon_c statically. */
#if defined(AEGON_C_STATIC)
#  define AEGON_C_API
#else
#  define AEGON_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
#  define AEGON_C_BEGIN extern "C" {
#  define AEGON_C_END   }
#else
#  define AEGON_C_BEGIN
#  define AEGON_C_END
#endif

typedef void aegon_c_export_unused_t;

#endif /* AEGON_C_EXPORT_H */
