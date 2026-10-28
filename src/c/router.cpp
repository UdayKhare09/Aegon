#include <aegon/c/router.h>
#include "internal.h"
#include "http/Router.h"
#include <memory>
#include <string>

using aegon::c::make_cleanup_holder;

extern "C" {

aegon_router_t* aegon_router_create(void) {
    AEGON_C_TRY {
        return aegon::c::to_c(new aegon::http::Router());
    } AEGON_C_CATCH_VAL(nullptr)
}

void aegon_router_destroy(aegon_router_t* router) {
    if (!router) return;
    delete aegon::c::to_cpp(router);
}

aegon_err_t aegon_router_add(aegon_router_t* router,
                             aegon_method_t method,
                             const char* pattern,
                             aegon_handler_fn handler,
                             void* userdata,
                             aegon_cleanup_fn cleanup) {
    if (!router || !pattern || !handler) return AEGON_ERR_INVALID_ARG;
    if (static_cast<uint8_t>(method) > static_cast<uint8_t>(aegon::http::Method::UNKNOWN)) {
        return AEGON_ERR_INVALID_ARG;
    }

    AEGON_C_TRY {
        auto holder = make_cleanup_holder(userdata, cleanup);
        auto cpp_method = static_cast<aegon::http::Method>(method);
        aegon::c::to_cpp(router)->add_route(cpp_method, pattern, [handler, userdata, holder](aegon::http::Context& ctx) {
            handler(aegon::c::to_c(&ctx), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_router_get(aegon_router_t* router, const char* pattern,
                             aegon_handler_fn handler, void* userdata,
                             aegon_cleanup_fn cleanup) {
    return aegon_router_add(router, AEGON_METHOD_GET, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_router_post(aegon_router_t* router, const char* pattern,
                              aegon_handler_fn handler, void* userdata,
                              aegon_cleanup_fn cleanup) {
    return aegon_router_add(router, AEGON_METHOD_POST, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_router_put(aegon_router_t* router, const char* pattern,
                             aegon_handler_fn handler, void* userdata,
                             aegon_cleanup_fn cleanup) {
    return aegon_router_add(router, AEGON_METHOD_PUT, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_router_del(aegon_router_t* router, const char* pattern,
                             aegon_handler_fn handler, void* userdata,
                             aegon_cleanup_fn cleanup) {
    return aegon_router_add(router, AEGON_METHOD_DELETE, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_router_patch(aegon_router_t* router, const char* pattern,
                               aegon_handler_fn handler, void* userdata,
                               aegon_cleanup_fn cleanup) {
    return aegon_router_add(router, AEGON_METHOD_PATCH, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_router_all(aegon_router_t* router, const char* pattern,
                             aegon_handler_fn handler, void* userdata,
                             aegon_cleanup_fn cleanup) {
    if (!router || !pattern || !handler) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        auto holder = make_cleanup_holder(userdata, cleanup);
        aegon::c::to_cpp(router)->all(pattern, [handler, userdata, holder](aegon::http::Context& ctx) {
            handler(aegon::c::to_c(&ctx), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_router_on_error(aegon_router_t* router,
                                  aegon_error_handler_fn handler,
                                  void* userdata,
                                  aegon_cleanup_fn cleanup) {
    if (!router || !handler) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        auto holder = make_cleanup_holder(userdata, cleanup);
        aegon::c::to_cpp(router)->set_error_handler([handler, userdata, holder](aegon::http::Context& ctx, std::exception_ptr ep) {
            std::string what;
            if (ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (const std::exception& e) {
                    what = e.what();
                } catch (...) {
                    what = "unknown non-std exception";
                }
            }
            handler(aegon::c::to_c(&ctx), aegon::c::to_c_str(what), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_router_on_not_found(aegon_router_t* router,
                                      aegon_handler_fn handler,
                                      void* userdata,
                                      aegon_cleanup_fn cleanup) {
    if (!router || !handler) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        auto holder = make_cleanup_holder(userdata, cleanup);
        aegon::c::to_cpp(router)->set_not_found_handler([handler, userdata, holder](aegon::http::Context& ctx) {
            handler(aegon::c::to_c(&ctx), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_router_on_method_not_allowed(aegon_router_t* router,
                                               aegon_handler_fn handler,
                                               void* userdata,
                                               aegon_cleanup_fn cleanup) {
    if (!router || !handler) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        auto holder = make_cleanup_holder(userdata, cleanup);
        aegon::c::to_cpp(router)->set_method_not_allowed_handler([handler, userdata, holder](aegon::http::Context& ctx) {
            handler(aegon::c::to_c(&ctx), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_group_t* aegon_router_group(aegon_router_t* router, const char* prefix) {
    if (!router) return nullptr;
    AEGON_C_TRY {
        auto grp = aegon::c::to_cpp(router)->group(prefix ? prefix : "");
        auto* handle = new aegon_group(std::move(grp));
        return aegon::c::to_c(handle);
    } AEGON_C_CATCH_VAL(nullptr)
}

aegon_group_t* aegon_group_group(aegon_group_t* parent, const char* sub_prefix) {
    if (!parent) return nullptr;
    AEGON_C_TRY {
        auto* ph = aegon::c::to_group_handle(parent);
        auto grp = ph->grp.group(sub_prefix ? sub_prefix : "");
        auto* handle = new aegon_group(std::move(grp));
        return aegon::c::to_c(handle);
    } AEGON_C_CATCH_VAL(nullptr)
}

void aegon_group_destroy(aegon_group_t* group) {
    if (!group) return;
    delete aegon::c::to_group_handle(group);
}

aegon_err_t aegon_group_add(aegon_group_t* group, aegon_method_t method, const char* pattern,
                            aegon_handler_fn handler, void* userdata, aegon_cleanup_fn cleanup) {
    if (!group || !pattern || !handler) return AEGON_ERR_INVALID_ARG;
    if (static_cast<uint8_t>(method) > static_cast<uint8_t>(aegon::http::Method::UNKNOWN)) {
        return AEGON_ERR_INVALID_ARG;
    }

    AEGON_C_TRY {
        auto holder = make_cleanup_holder(userdata, cleanup);
        auto cpp_method = static_cast<aegon::http::Method>(method);
        aegon::c::to_group_handle(group)->grp.add_route(cpp_method, pattern, [handler, userdata, holder](aegon::http::Context& ctx) {
            handler(aegon::c::to_c(&ctx), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_get(aegon_group_t* group, const char* pattern,
                            aegon_handler_fn handler, void* userdata,
                            aegon_cleanup_fn cleanup) {
    return aegon_group_add(group, AEGON_METHOD_GET, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_group_post(aegon_group_t* group, const char* pattern,
                             aegon_handler_fn handler, void* userdata,
                             aegon_cleanup_fn cleanup) {
    return aegon_group_add(group, AEGON_METHOD_POST, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_group_put(aegon_group_t* group, const char* pattern,
                            aegon_handler_fn handler, void* userdata,
                            aegon_cleanup_fn cleanup) {
    return aegon_group_add(group, AEGON_METHOD_PUT, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_group_del(aegon_group_t* group, const char* pattern,
                            aegon_handler_fn handler, void* userdata,
                            aegon_cleanup_fn cleanup) {
    return aegon_group_add(group, AEGON_METHOD_DELETE, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_group_patch(aegon_group_t* group, const char* pattern,
                              aegon_handler_fn handler, void* userdata,
                              aegon_cleanup_fn cleanup) {
    return aegon_group_add(group, AEGON_METHOD_PATCH, pattern, handler, userdata, cleanup);
}

aegon_err_t aegon_group_all(aegon_group_t* group, const char* pattern,
                            aegon_handler_fn handler, void* userdata,
                            aegon_cleanup_fn cleanup) {
    if (!group || !pattern || !handler) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        auto holder = make_cleanup_holder(userdata, cleanup);
        aegon::c::to_group_handle(group)->grp.all(pattern, [handler, userdata, holder](aegon::http::Context& ctx) {
            handler(aegon::c::to_c(&ctx), userdata);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"
