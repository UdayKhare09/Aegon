#include <aegon/c/async.h>
#include "internal.h"
#include "http/Router.h"
#include "http/RouteGroup.h"

extern "C" {

aegon_async_t* aegon_async_create(aegon_ctx_t* ctx,
                                  aegon_async_done_fn done,
                                  void* userdata) {
    if (!ctx) {
        aegon::c::set_last_error("aegon_async_create: ctx is NULL");
        return nullptr;
    }
    auto* c = aegon::c::to_cpp(ctx);
    auto* ptr = c->get<std::shared_ptr<aegon_async>>();
    if (!ptr || !*ptr) {
        aegon::c::set_last_error("aegon_async_create: route was not registered with aegon_router_add_async");
        return nullptr;
    }
    auto token = *ptr;
    if (token->created.exchange(true, std::memory_order_acq_rel)) {
        aegon::c::set_last_error("aegon_async_create: async token already created for this request");
        return nullptr;
    }
    token->done_fn = done;
    token->userdata = userdata;
    return reinterpret_cast<aegon_async_t*>(token.get());
}

void aegon_async_complete(aegon_async_t* token) {
    if (!token) return;
    auto* t = reinterpret_cast<aegon_async*>(token);
    auto s = t->shared_from_this();
    bool prev = s->completed.exchange(true, std::memory_order_acq_rel);
    if (prev) {
        // Contract violation: one-shot completion called twice
        return;
    }

    bool post_now = false;
    {
        std::lock_guard<std::mutex> lock(s->mtx);
        if (s->suspended.load(std::memory_order_acquire)) {
            post_now = true;
        }
    }

    if (post_now) {
        s->loop->post([s]() {
            s->resume_on_loop();
        });
    }
}

aegon_err_t aegon_router_add_async(aegon_router_t* router,
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
        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        auto cpp_method = static_cast<aegon::http::Method>(method);
        aegon::c::to_cpp(router)->add_route(cpp_method, pattern, [handler, userdata, holder](aegon::http::Context& ctx) -> aegon::core::Task<void> {
            auto token = std::make_shared<aegon_async>();
            token->loop = aegon::core::EventLoop::current();
            token->ctx = &ctx;
            ctx.set<std::shared_ptr<aegon_async>>(token);

            if (handler) {
                handler(aegon::c::to_c(&ctx), userdata);
            }

            if (token->created.load(std::memory_order_acquire)) {
                co_await token->awaiter();
            }
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_add_async(aegon_group_t* group,
                                  aegon_method_t method,
                                  const char* pattern,
                                  aegon_handler_fn handler,
                                  void* userdata,
                                  aegon_cleanup_fn cleanup) {
    if (!group || !pattern || !handler) return AEGON_ERR_INVALID_ARG;
    if (static_cast<uint8_t>(method) > static_cast<uint8_t>(aegon::http::Method::UNKNOWN)) {
        return AEGON_ERR_INVALID_ARG;
    }

    AEGON_C_TRY {
        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        auto cpp_method = static_cast<aegon::http::Method>(method);
        aegon::c::to_group_handle(group)->grp.add_route(cpp_method, pattern, [handler, userdata, holder](aegon::http::Context& ctx) -> aegon::core::Task<void> {
            auto token = std::make_shared<aegon_async>();
            token->loop = aegon::core::EventLoop::current();
            token->ctx = &ctx;
            ctx.set<std::shared_ptr<aegon_async>>(token);

            if (handler) {
                handler(aegon::c::to_c(&ctx), userdata);
            }

            if (token->created.load(std::memory_order_acquire)) {
                co_await token->awaiter();
            }
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"
