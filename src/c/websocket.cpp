#include <aegon/c/websocket.h>
#include "internal.h"
#include "http/Router.h"
#include "http/RouteGroup.h"
#include "http/websocket/WebSocket.h"
#include <cstring>
#include <vector>

namespace {

struct aegon_ws_wrapper {
    aegon::http::websocket::WebSocket& ws;
    std::vector<aegon::core::Task<void>> pending_sends;
};

thread_local aegon_ws_wrapper* tl_current_ws_wrapper = nullptr;

void copy_callbacks(aegon_ws_callbacks_t& dst, const aegon_ws_callbacks_t* src) {
    std::memset(&dst, 0, sizeof(dst));
    if (!src) return;
    size_t sz = src->struct_size;
    if (sz == 0) sz = sizeof(*src);
    #define AEGON_WS_CP(f) if (sz >= offsetof(aegon_ws_callbacks_t, f) + sizeof(src->f)) dst.f = src->f
    AEGON_WS_CP(on_open);
    AEGON_WS_CP(on_message);
    AEGON_WS_CP(on_close);
    AEGON_WS_CP(on_error);
    #undef AEGON_WS_CP
    dst.struct_size = sizeof(dst);
}

} // anonymous namespace

extern "C" {

void aegon_ws_callbacks_init(aegon_ws_callbacks_t* cbs) {
    if (!cbs) return;
    std::memset(cbs, 0, sizeof(*cbs));
    cbs->struct_size = sizeof(*cbs);
}

aegon_err_t aegon_router_ws(aegon_router_t* router,
                            const char* pattern,
                            const aegon_ws_callbacks_t* callbacks,
                            void* userdata,
                            aegon_cleanup_fn cleanup) {
    if (!router || !pattern || !callbacks) return AEGON_ERR_INVALID_ARG;

    AEGON_C_TRY {
        aegon_ws_callbacks_t cbs{};
        copy_callbacks(cbs, callbacks);
        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);

        aegon::c::to_cpp(router)->ws(pattern, [cbs, userdata, holder](aegon::http::websocket::WebSocket& ws) -> aegon::core::Task<void> {
            if (cbs.on_open) {
                aegon_ws_wrapper wrapper{ws, {}};
                tl_current_ws_wrapper = &wrapper;
                cbs.on_open(aegon::c::to_c(&ws), aegon::c::to_c_str(ws.path()), userdata);
                tl_current_ws_wrapper = nullptr;
                for (auto& t : wrapper.pending_sends) {
                    co_await std::move(t);
                }
                wrapper.pending_sends.clear();
            }

            if (cbs.on_message) {
                ws.on_message([cbs, userdata, holder](aegon::http::websocket::WebSocket& ws, aegon::http::websocket::Message msg) -> aegon::core::Task<void> {
                    aegon_ws_wrapper wrapper{ws, {}};
                    tl_current_ws_wrapper = &wrapper;
                    aegon_ws_opcode_t op = (msg.opcode() == aegon::http::websocket::Opcode::Binary) ? AEGON_WS_BINARY : AEGON_WS_TEXT;
                    cbs.on_message(aegon::c::to_c(&ws), aegon::c::to_c_str(msg.payload()), op, userdata);
                    tl_current_ws_wrapper = nullptr;
                    for (auto& t : wrapper.pending_sends) {
                        co_await std::move(t);
                    }
                    wrapper.pending_sends.clear();
                    co_return;
                });
            }

            if (cbs.on_close) {
                ws.on_close([cbs, userdata, holder](aegon::http::websocket::WebSocket& ws, aegon::http::websocket::CloseCode code, std::string_view reason) -> aegon::core::Task<void> {
                    aegon_ws_wrapper wrapper{ws, {}};
                    tl_current_ws_wrapper = &wrapper;
                    cbs.on_close(aegon::c::to_c(&ws), static_cast<uint16_t>(code), aegon::c::to_c_str(reason), userdata);
                    tl_current_ws_wrapper = nullptr;
                    for (auto& t : wrapper.pending_sends) {
                        co_await std::move(t);
                    }
                    wrapper.pending_sends.clear();
                    co_return;
                });
            }

            if (cbs.on_error) {
                ws.on_error([cbs, userdata, holder](aegon::http::websocket::WebSocket& ws, const std::exception& e) {
                    cbs.on_error(aegon::c::to_c(&ws), aegon::c::to_c_str(e.what()), userdata);
                });
            }

            co_return;
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_group_ws(aegon_group_t* group,
                           const char* pattern,
                           const aegon_ws_callbacks_t* callbacks,
                           void* userdata,
                           aegon_cleanup_fn cleanup) {
    if (!group || !pattern || !callbacks) return AEGON_ERR_INVALID_ARG;

    AEGON_C_TRY {
        auto* gh = aegon::c::to_group_handle(group);
        std::string full_path = aegon::http::join_paths(gh->grp.prefix(), pattern);
        return aegon_router_ws(aegon::c::to_c(&gh->grp.router()), full_path.c_str(), callbacks, userdata, cleanup);
    } AEGON_C_CATCH
}

aegon_err_t aegon_router_ws_echo(aegon_router_t* router, const char* pattern) {
    if (!router || !pattern) return AEGON_ERR_INVALID_ARG;
    AEGON_C_TRY {
        aegon::c::to_cpp(router)->ws_echo(pattern);
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_ws_send_text(aegon_ws_t* ws, aegon_str_t payload) {
    if (!ws) return AEGON_ERR_INVALID_ARG;
    auto* w = aegon::c::to_cpp(ws);
    if (!w->is_open()) return AEGON_ERR_STATE;

    std::string_view sv = aegon::c::to_sv(payload);
    if (tl_current_ws_wrapper && &tl_current_ws_wrapper->ws == w) {
        tl_current_ws_wrapper->pending_sends.push_back(w->send_text(sv));
        return AEGON_OK;
    }
    if (auto* loop = aegon::core::EventLoop::current()) {
        loop->spawn(w->send_text(sv));
        return AEGON_OK;
    }
    return aegon::c::record_error("ws_send_text called outside active event loop or session callback", AEGON_ERR_STATE);
}

aegon_err_t aegon_ws_send_binary(aegon_ws_t* ws, aegon_str_t payload) {
    if (!ws) return AEGON_ERR_INVALID_ARG;
    auto* w = aegon::c::to_cpp(ws);
    if (!w->is_open()) return AEGON_ERR_STATE;

    std::span<const uint8_t> span(reinterpret_cast<const uint8_t*>(payload.data ? payload.data : ""), payload.len);
    if (tl_current_ws_wrapper && &tl_current_ws_wrapper->ws == w) {
        tl_current_ws_wrapper->pending_sends.push_back(w->send_binary(span));
        return AEGON_OK;
    }
    if (auto* loop = aegon::core::EventLoop::current()) {
        loop->spawn(w->send_binary(span));
        return AEGON_OK;
    }
    return aegon::c::record_error("ws_send_binary called outside active event loop or session callback", AEGON_ERR_STATE);
}

aegon_err_t aegon_ws_send_ping(aegon_ws_t* ws, aegon_str_t payload) {
    if (!ws) return AEGON_ERR_INVALID_ARG;
    auto* w = aegon::c::to_cpp(ws);
    if (!w->is_open()) return AEGON_ERR_STATE;

    std::string_view sv = aegon::c::to_sv(payload);
    if (tl_current_ws_wrapper && &tl_current_ws_wrapper->ws == w) {
        tl_current_ws_wrapper->pending_sends.push_back(w->send_ping(sv));
        return AEGON_OK;
    }
    if (auto* loop = aegon::core::EventLoop::current()) {
        loop->spawn(w->send_ping(sv));
        return AEGON_OK;
    }
    return aegon::c::record_error("ws_send_ping called outside active event loop or session callback", AEGON_ERR_STATE);
}

aegon_err_t aegon_ws_send_pong(aegon_ws_t* ws, aegon_str_t payload) {
    if (!ws) return AEGON_ERR_INVALID_ARG;
    auto* w = aegon::c::to_cpp(ws);
    if (!w->is_open()) return AEGON_ERR_STATE;

    std::string_view sv = aegon::c::to_sv(payload);
    if (tl_current_ws_wrapper && &tl_current_ws_wrapper->ws == w) {
        tl_current_ws_wrapper->pending_sends.push_back(w->send_pong(sv));
        return AEGON_OK;
    }
    if (auto* loop = aegon::core::EventLoop::current()) {
        loop->spawn(w->send_pong(sv));
        return AEGON_OK;
    }
    return aegon::c::record_error("ws_send_pong called outside active event loop or session callback", AEGON_ERR_STATE);
}

aegon_err_t aegon_ws_close(aegon_ws_t* ws, uint16_t code, aegon_str_t reason) {
    if (!ws) return AEGON_ERR_INVALID_ARG;
    auto* w = aegon::c::to_cpp(ws);
    if (!w->is_open()) return AEGON_OK;

    auto close_code = static_cast<aegon::http::websocket::CloseCode>(code);
    std::string_view sv = aegon::c::to_sv(reason);
    if (tl_current_ws_wrapper && &tl_current_ws_wrapper->ws == w) {
        tl_current_ws_wrapper->pending_sends.push_back(w->close(close_code, sv));
        return AEGON_OK;
    }
    if (auto* loop = aegon::core::EventLoop::current()) {
        loop->spawn(w->close(close_code, sv));
        return AEGON_OK;
    }
    return aegon::c::record_error("ws_close called outside active event loop or session callback", AEGON_ERR_STATE);
}

bool aegon_ws_is_open(aegon_ws_t* ws) {
    if (!ws) return false;
    return aegon::c::to_cpp(ws)->is_open();
}

} // extern "C"

