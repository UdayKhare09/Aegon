#include <aegon/c/middleware.h>
#include "internal.h"
#include "http/Router.h"
#include "http/RouteGroup.h"
#include "http/Middleware.h"
#include <vector>
#include <memory>

namespace aegon::c {

inline http::MiddlewareFn wrap_c_middleware(aegon_middleware_fn fn, void* userdata, std::shared_ptr<CleanupHolder> cleanup_sp) {
    return [fn, userdata, cleanup_sp](http::Context& ctx, http::Next next) -> core::Task<void> {
        aegon_next next_holder{&next, false};
        fn(to_c(&ctx), to_c(&next_holder), userdata);
        co_return;
    };
}

} // namespace aegon::c

extern "C" {

void aegon_next_call(aegon_next_t* next, aegon_ctx_t* ctx) {
    if (!next || !ctx) return;
    auto* h = aegon::c::to_next_handle(next);
    if (!h || !h->next_fn) return;
    h->called = true;
    auto task = (*h->next_fn)(*aegon::c::to_cpp(ctx));
    task.resume();
    task.result();
}

aegon_err_t aegon_router_use(aegon_router_t* router,
                             aegon_middleware_fn mw,
                             void* userdata,
                             aegon_cleanup_fn cleanup) {
    if (!router || !mw) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        aegon::c::to_cpp(router)->use(aegon::c::wrap_c_middleware(mw, userdata, holder));
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_use(aegon_group_t* group,
                            aegon_middleware_fn mw,
                            void* userdata,
                            aegon_cleanup_fn cleanup) {
    if (!group || !mw) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        aegon::c::to_group_handle(group)->grp.use(aegon::c::wrap_c_middleware(mw, userdata, holder));
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_router_add_mw(aegon_router_t* router,
                                aegon_method_t method,
                                const char* pattern,
                                const aegon_middleware_slot_t* mws,
                                size_t mw_count,
                                aegon_handler_fn handler,
                                void* userdata,
                                aegon_cleanup_fn cleanup) {
    if (!router || !pattern || !handler) return AEGON_ERR_INVALID_ARG;
    if (mw_count > 0 && !mws) return AEGON_ERR_INVALID_ARG;
    if (static_cast<uint8_t>(method) > static_cast<uint8_t>(aegon::http::Method::UNKNOWN)) {
        return AEGON_ERR_INVALID_ARG;
    }

    AEGON_C_TRY {
        std::vector<aegon::http::MiddlewareFn> route_mw;
        route_mw.reserve(mw_count);
        for (size_t i = 0; i < mw_count; ++i) {
            if (!mws[i].fn) return AEGON_ERR_INVALID_ARG;
            auto holder = aegon::c::make_cleanup_holder(mws[i].userdata, mws[i].cleanup);
            route_mw.push_back(aegon::c::wrap_c_middleware(mws[i].fn, mws[i].userdata, holder));
        }

        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        auto cpp_method = static_cast<aegon::http::Method>(method);
        aegon::c::to_cpp(router)->add_route(cpp_method, pattern, std::move(route_mw),
                                            [handler, userdata, holder](aegon::http::Context& ctx) {
            handler(aegon::c::to_c(&ctx), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_add_mw(aegon_group_t* group,
                              aegon_method_t method,
                              const char* pattern,
                              const aegon_middleware_slot_t* mws,
                              size_t mw_count,
                              aegon_handler_fn handler,
                              void* userdata,
                              aegon_cleanup_fn cleanup) {
    if (!group || !pattern || !handler) return AEGON_ERR_INVALID_ARG;
    if (mw_count > 0 && !mws) return AEGON_ERR_INVALID_ARG;
    if (static_cast<uint8_t>(method) > static_cast<uint8_t>(aegon::http::Method::UNKNOWN)) {
        return AEGON_ERR_INVALID_ARG;
    }

    AEGON_C_TRY {
        std::vector<aegon::http::MiddlewareFn> route_mw;
        route_mw.reserve(mw_count);
        for (size_t i = 0; i < mw_count; ++i) {
            if (!mws[i].fn) return AEGON_ERR_INVALID_ARG;
            auto holder = aegon::c::make_cleanup_holder(mws[i].userdata, mws[i].cleanup);
            route_mw.push_back(aegon::c::wrap_c_middleware(mws[i].fn, mws[i].userdata, holder));
        }

        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        auto cpp_method = static_cast<aegon::http::Method>(method);
        aegon::c::to_group_handle(group)->grp.add_route(cpp_method, pattern, std::move(route_mw),
                                                        [handler, userdata, holder](aegon::http::Context& ctx) {
            handler(aegon::c::to_c(&ctx), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"

