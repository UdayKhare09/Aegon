#include <aegon/c/sse.h>
#include "internal.h"
#include <string>

extern "C" {

aegon_sse_t* aegon_sse_create(aegon_ctx_t* ctx,
                              aegon_sse_on_close_fn on_close,
                              void* userdata) {
    if (!ctx) {
        aegon::c::set_last_error("aegon_sse_create: ctx is NULL");
        return nullptr;
    }
    auto* c = aegon::c::to_cpp(ctx);
    auto* ptr = c->get<std::shared_ptr<aegon_async>>();
    if (!ptr || !*ptr) {
        aegon::c::set_last_error("aegon_sse_create: route was not registered with aegon_router_add_async");
        return nullptr;
    }
    auto token = *ptr;
    if (token->created.exchange(true, std::memory_order_acq_rel)) {
        aegon::c::set_last_error("aegon_sse_create: async token already created for this request");
        return nullptr;
    }

    AEGON_C_TRY {
        auto impl = std::make_shared<aegon_sse_impl>();
        impl->loop = token->loop;
        impl->async_token = token;
        impl->stream = c->create_sse_stream();
        impl->on_close = on_close;
        impl->userdata = userdata;
        impl->is_open.store(true, std::memory_order_release);

        auto* handle = new aegon_sse();
        handle->impl = impl;
        return aegon::c::to_c(handle);
    } AEGON_C_CATCH_VAL(nullptr)
}

aegon_err_t aegon_sse_send_event(aegon_sse_t* sse, aegon_str_t name, aegon_str_t data) {
    if (!sse) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_sse_handle(sse);
    if (!h || !h->impl) return AEGON_ERR_INVALID_ARG;
    if (!h->impl->is_open.load(std::memory_order_acquire)) return AEGON_ERR_STATE;

    std::string frame;
    frame.reserve(name.len + data.len + 16);
    frame.append("event: ");
    frame.append(name.data ? name.data : "", name.len);
    frame.push_back('\n');
    frame.append("data: ");
    frame.append(data.data ? data.data : "", data.len);
    frame.append("\n\n");

    h->impl->send_frame(std::move(frame));
    return AEGON_OK;
}

aegon_err_t aegon_sse_send_data(aegon_sse_t* sse, aegon_str_t data) {
    if (!sse) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_sse_handle(sse);
    if (!h || !h->impl) return AEGON_ERR_INVALID_ARG;
    if (!h->impl->is_open.load(std::memory_order_acquire)) return AEGON_ERR_STATE;

    std::string frame;
    frame.reserve(data.len + 8);
    frame.append("data: ");
    frame.append(data.data ? data.data : "", data.len);
    frame.append("\n\n");

    h->impl->send_frame(std::move(frame));
    return AEGON_OK;
}

aegon_err_t aegon_sse_send_comment(aegon_sse_t* sse, aegon_str_t text) {
    if (!sse) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_sse_handle(sse);
    if (!h || !h->impl) return AEGON_ERR_INVALID_ARG;
    if (!h->impl->is_open.load(std::memory_order_acquire)) return AEGON_ERR_STATE;

    std::string frame;
    frame.reserve(text.len + 4);
    frame.append(": ");
    frame.append(text.data ? text.data : "", text.len);
    frame.append("\n\n");

    h->impl->send_frame(std::move(frame));
    return AEGON_OK;
}

aegon_err_t aegon_sse_send_retry(aegon_sse_t* sse, uint32_t milliseconds) {
    if (!sse) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_sse_handle(sse);
    if (!h || !h->impl) return AEGON_ERR_INVALID_ARG;
    if (!h->impl->is_open.load(std::memory_order_acquire)) return AEGON_ERR_STATE;

    std::string frame = "retry: " + std::to_string(milliseconds) + "\n\n";
    h->impl->send_frame(std::move(frame));
    return AEGON_OK;
}

void aegon_sse_close(aegon_sse_t* sse) {
    if (!sse) return;
    auto* h = aegon::c::to_sse_handle(sse);
    if (!h || !h->impl) return;

    bool expected = true;
    if (h->impl->is_open.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        h->impl->loop->post([s = h->impl]() {
            if (s->stream) {
                s->stream->close();
            }
            if (s->async_token) {
                aegon_async_complete(s->async_token.get());
            }
        });
    }
}

bool aegon_sse_is_open(aegon_sse_t* sse) {
    if (!sse) return false;
    auto* h = aegon::c::to_sse_handle(sse);
    if (!h || !h->impl) return false;
    return h->impl->is_open.load(std::memory_order_acquire);
}

void aegon_sse_destroy(aegon_sse_t* sse) {
    if (!sse) return;
    delete aegon::c::to_sse_handle(sse);
}

} // extern "C"

