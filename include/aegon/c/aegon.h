#ifndef AEGON_C_AEGON_H
#define AEGON_C_AEGON_H

#include <aegon/c/export.h>
#include <aegon/c/types.h>
#include <aegon/c/error.h>
#include <aegon/c/log.h>
#include <aegon/c/request.h>
#include <aegon/c/response.h>
#include <aegon/c/context.h>
#include <aegon/c/router.h>
#include <aegon/c/server.h>
#include <aegon/c/middleware.h>
#include <aegon/c/cors.h>
#include <aegon/c/compress.h>
#include <aegon/c/security.h>
#include <aegon/c/access_log.h>
#include <aegon/c/static_files.h>
#include <aegon/c/multipart.h>
#include <aegon/c/async.h>
#include <aegon/c/websocket.h>
#include <aegon/c/sse.h>
/* Phase 4: grpc.h
 * (included here as each phase lands; #include order is dependency-free) */

AEGON_C_BEGIN

/* Runtime version of the linked library (may differ from headers). */
AEGON_C_API uint32_t    aegon_version(void);
AEGON_C_API const char* aegon_version_string(void);

/* ABI epoch actually exported by the linked library (== AEGON_ABI_VERSION). */
AEGON_C_API uint32_t    aegon_abi_version(void);

/* HTTP status code macros (common subset; any uint16_t is accepted). */
#define AEGON_STATUS_CONTINUE            100u
#define AEGON_STATUS_SWITCHING_PROTOCOLS 101u
#define AEGON_STATUS_OK                  200u
#define AEGON_STATUS_CREATED             201u
#define AEGON_STATUS_ACCEPTED            202u
#define AEGON_STATUS_NO_CONTENT          204u
#define AEGON_STATUS_PARTIAL_CONTENT     206u
#define AEGON_STATUS_MOVED_PERMANENTLY   301u
#define AEGON_STATUS_FOUND               302u
#define AEGON_STATUS_SEE_OTHER           303u
#define AEGON_STATUS_NOT_MODIFIED        304u
#define AEGON_STATUS_TEMPORARY_REDIRECT  307u
#define AEGON_STATUS_PERMANENT_REDIRECT  308u
#define AEGON_STATUS_BAD_REQUEST         400u
#define AEGON_STATUS_UNAUTHORIZED        401u
#define AEGON_STATUS_FORBIDDEN           403u
#define AEGON_STATUS_NOT_FOUND           404u
#define AEGON_STATUS_METHOD_NOT_ALLOWED  405u
#define AEGON_STATUS_REQUEST_TIMEOUT     408u
#define AEGON_STATUS_CONFLICT            409u
#define AEGON_STATUS_PAYLOAD_TOO_LARGE   413u
#define AEGON_STATUS_URI_TOO_LONG        414u
#define AEGON_STATUS_UNPROCESSABLE_ENTITY 422u
#define AEGON_STATUS_UPGRADE_REQUIRED    426u
#define AEGON_STATUS_TOO_MANY_REQUESTS   429u
#define AEGON_STATUS_INTERNAL_SERVER_ERROR 500u
#define AEGON_STATUS_NOT_IMPLEMENTED     501u
#define AEGON_STATUS_BAD_GATEWAY         502u
#define AEGON_STATUS_SERVICE_UNAVAILABLE 503u
#define AEGON_STATUS_GATEWAY_TIMEOUT     504u

/* Reason phrase for any status code ("OK", "Not Found", ...). Static storage.
 * Unknown codes return "Unknown". Mirrors aegon::http::status_phrase. */
AEGON_C_API const char* aegon_status_phrase(uint16_t status);

/* Method <-> string helpers. cstr is static storage ("GET", ...).
 * from_str is case-sensitive per RFC 9110; unknown -> AEGON_METHOD_UNKNOWN. */
AEGON_C_API const char*     aegon_method_cstr(aegon_method_t m);
AEGON_C_API aegon_method_t  aegon_method_from_str(aegon_str_t s);

/* HTTP version string: static storage ("HTTP/1.1", "HTTP/2.0", "HTTP/3.0"). */
AEGON_C_API const char* aegon_http_version_cstr(aegon_http_version_t v);

AEGON_C_END

#endif /* AEGON_C_AEGON_H */

