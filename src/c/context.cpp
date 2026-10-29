#include <aegon/c/context.h>
#include "internal.h"
#include "http/Context.h"
#include "http/Request.h"
#include "http/Response.h"

extern "C" {

aegon_req_t* aegon_ctx_req(aegon_ctx_t* ctx) {
    if (!ctx) return nullptr;
    return aegon::c::to_c(&aegon::c::to_cpp(ctx)->req());
}

aegon_res_t* aegon_ctx_res(aegon_ctx_t* ctx) {
    if (!ctx) return nullptr;
    return aegon::c::to_c(&aegon::c::to_cpp(ctx)->res());
}

void* aegon_ctx_app_data(aegon_ctx_t* ctx) {
    if (!ctx) return nullptr;
    auto* app_data = aegon::c::to_cpp(ctx)->try_service<aegon::c::CAppData>();
    return app_data ? app_data->ptr : nullptr;
}

aegon_err_t aegon_ctx_problem(aegon_ctx_t* ctx, uint16_t status,
                              aegon_str_t title, aegon_str_t detail) {
    if (!ctx) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::c::to_cpp(ctx)->problem(
            static_cast<aegon::http::StatusCode>(status),
            aegon::c::to_sv(title),
            aegon::c::to_sv(detail)
        );
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_ctx_send_file(aegon_ctx_t* ctx, const char* path,
                                aegon_str_t mime) {
    if (!ctx || !path) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::c::to_cpp(ctx)->send_file(path, aegon::c::to_sv(mime));
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"

