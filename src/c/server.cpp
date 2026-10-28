#include "http/Server.h"
#include "http/ServerConfig.h"
#include "http/Router.h"
#include "internal.h"
#include <aegon/c/server.h>

#include <cstddef>
#include <cstring>
#include <thread>
#include <algorithm>

aegon_server::aegon_server() = default;

aegon_server::~aegon_server() {
    if (supervisor_thread.joinable()) {
        if (server) server->stop();
        supervisor_thread.join();
    }
}

namespace aegon::c {
http::Server* to_cpp(aegon_server_t* s) noexcept {
    auto* h = to_server_handle(s);
    return h ? h->server.get() : nullptr;
}
}

namespace {

void copy_server_config(aegon::http::ServerConfig& target, const aegon_server_config_t* cfg) {
    if (!cfg || cfg->struct_size < sizeof(size_t)) return;
    const size_t sz = cfg->struct_size;

    #define AEGON_HAS_FIELD(m) (sz >= offsetof(aegon_server_config_t, m) + sizeof(cfg->m))

    if (AEGON_HAS_FIELD(engine)) {
        switch (cfg->engine) {
            case AEGON_ENGINE_IOURING: target.engine_type = aegon::core::EngineType::IoUring; break;
            case AEGON_ENGINE_EPOLL:   target.engine_type = aegon::core::EngineType::Epoll;   break;
            case AEGON_ENGINE_KQUEUE:  target.engine_type = aegon::core::EngineType::Kqueue;  break;
            case AEGON_ENGINE_IOCP:    target.engine_type = aegon::core::EngineType::Iocp;    break;
            case AEGON_ENGINE_AUTO:
            default:                   target.engine_type = aegon::core::EngineType::Auto;    break;
        }
    }

    if (AEGON_HAS_FIELD(ring_entries)) target.ring_entries = cfg->ring_entries;
    if (AEGON_HAS_FIELD(buffer_pool_entries)) target.buffer_pool_entries = cfg->buffer_pool_entries;
    if (AEGON_HAS_FIELD(buffer_size)) target.buffer_size = cfg->buffer_size;

    if (AEGON_HAS_FIELD(max_uri_length)) target.limits.max_uri_length = cfg->max_uri_length;
    if (AEGON_HAS_FIELD(max_headers_size)) target.limits.max_headers_size = cfg->max_headers_size;
    if (AEGON_HAS_FIELD(max_header_count)) target.limits.max_header_count = cfg->max_header_count;
    if (AEGON_HAS_FIELD(max_body_size)) target.limits.max_body_size = cfg->max_body_size;

    if (AEGON_HAS_FIELD(tcp_nodelay)) target.tcp.nodelay = cfg->tcp_nodelay;
    if (AEGON_HAS_FIELD(tcp_keepalive)) target.tcp.keepalive = cfg->tcp_keepalive;
    if (AEGON_HAS_FIELD(tcp_keepidle)) target.tcp.keepidle = cfg->tcp_keepidle;
    if (AEGON_HAS_FIELD(tcp_keepintvl)) target.tcp.keepintvl = cfg->tcp_keepintvl;
    if (AEGON_HAS_FIELD(tcp_keepcnt)) target.tcp.keepcnt = cfg->tcp_keepcnt;

    if (AEGON_HAS_FIELD(h2_max_concurrent_streams)) target.h2_max_concurrent_streams = cfg->h2_max_concurrent_streams;
    if (AEGON_HAS_FIELD(h2_initial_window_size)) target.h2_initial_window_size = cfg->h2_initial_window_size;
    if (AEGON_HAS_FIELD(rst_burst_limit)) target.rst_burst_limit = cfg->rst_burst_limit;

    if (AEGON_HAS_FIELD(ws_max_message_size)) target.websocket.max_message_size = cfg->ws_max_message_size;
    if (AEGON_HAS_FIELD(ws_max_frame_size)) target.websocket.max_frame_size = cfg->ws_max_frame_size;
    if (AEGON_HAS_FIELD(ws_require_masked_frames)) target.websocket.require_masked_frames = cfg->ws_require_masked_frames;
    if (AEGON_HAS_FIELD(ws_auto_ping_interval_sec)) target.websocket.auto_ping_interval_sec = cfg->ws_auto_ping_interval_sec;
    if (AEGON_HAS_FIELD(ws_ping_timeout_sec)) target.websocket.ping_timeout_sec = cfg->ws_ping_timeout_sec;
    if (AEGON_HAS_FIELD(ws_initial_buffer_capacity)) target.websocket.initial_buffer_capacity = cfg->ws_initial_buffer_capacity;

    if (AEGON_HAS_FIELD(enable_http3)) target.enable_http3 = cfg->enable_http3;

    #undef AEGON_HAS_FIELD
}

} // anonymous namespace

extern "C" {

void aegon_server_config_init(aegon_server_config_t* cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = sizeof(aegon_server_config_t);

    cfg->engine = AEGON_ENGINE_AUTO;
    cfg->ring_entries = 4096;
    cfg->buffer_pool_entries = 8192;
    cfg->buffer_size = 4096;

    cfg->max_uri_length = 8192;
    cfg->max_headers_size = 65536;
    cfg->max_header_count = 100;
    cfg->max_body_size = 16 * 1024 * 1024;

    cfg->tcp_nodelay = true;
    cfg->tcp_keepalive = true;
    cfg->tcp_keepidle = 30;
    cfg->tcp_keepintvl = 10;
    cfg->tcp_keepcnt = 3;

    cfg->h2_max_concurrent_streams = 256;
    cfg->h2_initial_window_size = 1048576; // 1 MiB
    cfg->rst_burst_limit = 1000;

    cfg->ws_max_message_size = 16 * 1024 * 1024;
    cfg->ws_max_frame_size = 16 * 1024 * 1024;
    cfg->ws_require_masked_frames = true;
    cfg->ws_auto_ping_interval_sec = 0;
    cfg->ws_ping_timeout_sec = 10;
    cfg->ws_initial_buffer_capacity = 4096;

    cfg->enable_http3 = true;
}

aegon_server_t* aegon_server_create(aegon_router_t** router,
                                    const aegon_server_config_t* cfg) {
    if (!router || !*router) {
        aegon::c::record_error("router pointer is null", AEGON_ERR_INVALID_ARG);
        return nullptr;
    }

    AEGON_C_TRY {
        auto* r = aegon::c::to_cpp(*router);
        auto srv = std::make_unique<aegon::http::Server>(std::move(*r));
        delete r;
        *router = nullptr;

        if (cfg) {
            aegon::http::ServerConfig sc = srv->config();
            copy_server_config(sc, cfg);
            srv->config(sc);
        }

        auto* handle = new aegon_server();
        handle->server = std::move(srv);
        return aegon::c::to_c(handle);
    } AEGON_C_CATCH_VAL(nullptr)
}

void aegon_server_destroy(aegon_server_t* server) {
    if (!server) return;
    delete aegon::c::to_server_handle(server);
}

aegon_err_t aegon_server_set_config(aegon_server_t* server,
                                    const aegon_server_config_t* cfg) {
    if (!server || !cfg) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_server_handle(server);
    if (!h || !h->server) return AEGON_ERR_INVALID_ARG;
    if (h->server->is_running() || h->async_running.load()) {
        return aegon::c::record_error("cannot configure running server", AEGON_ERR_STATE);
    }

    AEGON_C_TRY {
        aegon::http::ServerConfig sc = h->server->config();
        copy_server_config(sc, cfg);
        h->server->config(sc);
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_server_listen(aegon_server_t* server, uint16_t port,
                                const char* host) {
    if (!server || port == 0) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_server_handle(server);
    if (!h || !h->server) return AEGON_ERR_INVALID_ARG;
    if (h->server->is_running() || h->async_running.load()) {
        return aegon::c::record_error("cannot add listener to running server", AEGON_ERR_STATE);
    }

    AEGON_C_TRY {
        h->server->listen(port, host ? host : "0.0.0.0");
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_server_listen_tls(aegon_server_t* server, uint16_t port,
                                    const char* host) {
    if (!server || port == 0) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_server_handle(server);
    if (!h || !h->server) return AEGON_ERR_INVALID_ARG;
    if (h->server->is_running() || h->async_running.load()) {
        return aegon::c::record_error("cannot add listener to running server", AEGON_ERR_STATE);
    }

    AEGON_C_TRY {
        h->server->listen_tls(port, host ? host : "0.0.0.0");
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_server_enable_tls(aegon_server_t* server,
                                    const char* cert_file,
                                    const char* key_file) {
    if (!server) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_server_handle(server);
    if (!h || !h->server) return AEGON_ERR_INVALID_ARG;
    if (h->server->is_running() || h->async_running.load()) {
        return aegon::c::record_error("cannot configure TLS on running server", AEGON_ERR_STATE);
    }

    AEGON_C_TRY {
        h->server->enable_tls(cert_file ? cert_file : "", key_file ? key_file : "");
        return AEGON_OK;
    } catch (const std::exception& e) {
        return aegon::c::record_error(e.what(), AEGON_ERR_TLS);
    } catch (...) {
        return aegon::c::record_error("TLS configuration error", AEGON_ERR_TLS);
    }
}

aegon_err_t aegon_server_set_app_data(aegon_server_t* server, void* app_data) {
    if (!server) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_server_handle(server);
    if (!h || !h->server) return AEGON_ERR_INVALID_ARG;
    if (h->server->is_running() || h->async_running.load()) {
        return aegon::c::record_error("cannot set app data on running server", AEGON_ERR_STATE);
    }

    AEGON_C_TRY {
        h->app_data = app_data;
        h->server->provide<aegon::c::CAppData>(std::make_shared<aegon::c::CAppData>(aegon::c::CAppData{app_data}));
        return AEGON_OK;
    } AEGON_C_CATCH
}

void* aegon_server_app_data(aegon_server_t* server) {
    if (!server) return nullptr;
    auto* h = aegon::c::to_server_handle(server);
    return h ? h->app_data : nullptr;
}

aegon_err_t aegon_server_run(aegon_server_t* server, uint32_t threads) {
    if (!server) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_server_handle(server);
    if (!h || !h->server) return AEGON_ERR_INVALID_ARG;
    if (h->server->is_running() || h->async_running.load()) {
        return aegon::c::record_error("server is already running", AEGON_ERR_STATE);
    }
    if (h->server->listeners().empty()) {
        return aegon::c::record_error("no listeners configured", AEGON_ERR_STATE);
    }

    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 1;
    }

    AEGON_C_TRY {
        h->server->run(threads);
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_server_run_async(aegon_server_t* server, uint32_t threads) {
    if (!server) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_server_handle(server);
    if (!h || !h->server) return AEGON_ERR_INVALID_ARG;
    if (h->server->is_running() || h->async_running.load()) {
        return aegon::c::record_error("server is already running", AEGON_ERR_STATE);
    }
    if (h->server->listeners().empty()) {
        return aegon::c::record_error("no listeners configured", AEGON_ERR_STATE);
    }

    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 1;
    }

    AEGON_C_TRY {
        if (h->supervisor_thread.joinable()) {
            h->supervisor_thread.join();
        }
        h->async_running.store(true);
        h->supervisor_thread = std::thread([h, threads]() {
            try {
                h->server->run(threads);
            } catch (...) {}
            h->async_running.store(false);
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

void aegon_server_stop(aegon_server_t* server) {
    if (!server) return;
    auto* h = aegon::c::to_server_handle(server);
    if (h && h->server) {
        h->server->stop();
    }
}

aegon_err_t aegon_server_join(aegon_server_t* server) {
    if (!server) return AEGON_ERR_INVALID_ARG;
    auto* h = aegon::c::to_server_handle(server);
    if (!h) return AEGON_ERR_INVALID_ARG;

    AEGON_C_TRY {
        if (h->supervisor_thread.joinable()) {
            h->supervisor_thread.join();
        }
        h->async_running.store(false);
        return AEGON_OK;
    } AEGON_C_CATCH
}

bool aegon_server_is_running(aegon_server_t* server) {
    if (!server) return false;
    auto* h = aegon::c::to_server_handle(server);
    if (!h || !h->server) return false;
    return h->server->is_running() || h->async_running.load();
}

aegon_err_t aegon_server_on_start(aegon_server_t* server,
                                  aegon_lifecycle_fn fn,
                                  void* userdata,
                                  aegon_cleanup_fn cleanup) {
    if (!server || !fn) return AEGON_ERR_INVALID_ARG;
    auto* s = aegon::c::to_cpp(server);
    if (!s) return AEGON_ERR_INVALID_ARG;
    if (s->is_running()) return aegon::c::record_error("server is already running", AEGON_ERR_STATE);

    AEGON_C_TRY {
        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        s->on_start([server, fn, userdata, holder](aegon::http::Server&) -> aegon::core::Task<void> {
            fn(server, userdata);
            co_return;
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_server_on_stop(aegon_server_t* server,
                                 aegon_lifecycle_fn fn,
                                 void* userdata,
                                 aegon_cleanup_fn cleanup) {
    if (!server || !fn) return AEGON_ERR_INVALID_ARG;
    auto* s = aegon::c::to_cpp(server);
    if (!s) return AEGON_ERR_INVALID_ARG;
    if (s->is_running()) return aegon::c::record_error("server is already running", AEGON_ERR_STATE);

    AEGON_C_TRY {
        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        s->on_stop([server, fn, userdata, holder](aegon::http::Server&) -> aegon::core::Task<void> {
            fn(server, userdata);
            co_return;
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

aegon_err_t aegon_server_spawn_worker(aegon_server_t* server,
                                      aegon_worker_fn fn,
                                      void* userdata,
                                      aegon_cleanup_fn cleanup) {
    if (!server || !fn) return AEGON_ERR_INVALID_ARG;
    auto* s = aegon::c::to_cpp(server);
    if (!s) return AEGON_ERR_INVALID_ARG;
    if (s->is_running()) return aegon::c::record_error("server is already running", AEGON_ERR_STATE);

    AEGON_C_TRY {
        auto holder = aegon::c::make_cleanup_holder(userdata, cleanup);
        s->spawn_worker([fn, userdata, holder](aegon::http::Server&, aegon::core::EventLoop&) -> aegon::core::Task<void> {
            fn(userdata);
            co_return;
        });
        return AEGON_OK;
    } AEGON_C_CATCH
}

} // extern "C"
