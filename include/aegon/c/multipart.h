#ifndef AEGON_C_MULTIPART_H
#define AEGON_C_MULTIPART_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/context.h>

AEGON_C_BEGIN

typedef struct aegon_multipart aegon_multipart_t;   /* owns MultipartFormData */

/* Parse a multipart/form-data request.
 * Returns NULL when Content-Type is not multipart or the body is malformed.
 * The returned pointer is allocated; the parsed field/file slices borrow from
 * the request body buffer, so mp must be destroyed BEFORE the request ends,
 * OR caller must copy.
 * Free with aegon_multipart_destroy. */
AEGON_C_API aegon_multipart_t* aegon_multipart_parse(aegon_ctx_t* ctx);
AEGON_C_API void               aegon_multipart_destroy(aegon_multipart_t* mp);

/* Textual form fields */
AEGON_C_API aegon_str_t aegon_multipart_field(aegon_multipart_t* mp, const char* name);
AEGON_C_API size_t      aegon_multipart_field_count(aegon_multipart_t* mp);
AEGON_C_API bool        aegon_multipart_field_at(aegon_multipart_t* mp, size_t index,
                                                 aegon_str_t* out_name,
                                                 aegon_str_t* out_value);

/* Uploaded files */
typedef struct aegon_multipart_file_info {
    aegon_str_t name;          /* form field name */
    aegon_str_t filename;      /* client filename (e.g. "photo.jpg") */
    aegon_str_t content_type;  /* e.g. "image/jpeg" */
    aegon_str_t data;          /* raw payload bytes (borrowed) */
} aegon_multipart_file_info_t;

AEGON_C_API bool aegon_multipart_file(aegon_multipart_t* mp, const char* name,
                                      aegon_multipart_file_info_t* out_info);
AEGON_C_API size_t aegon_multipart_file_count(aegon_multipart_t* mp);
AEGON_C_API bool   aegon_multipart_file_at(aegon_multipart_t* mp, size_t index,
                                           aegon_multipart_file_info_t* out_info);

AEGON_C_END

#endif /* AEGON_C_MULTIPART_H */

