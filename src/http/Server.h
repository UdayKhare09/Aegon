#pragma once

#include "http/Router.h"
#include "core/EventLoop.h"
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <atomic>

namespace aegon::http {

class Server {
public:
    Server() = default;
    ~Server() = default;

    // Fluent routing methods
    Server& get(std::string_view pattern, Handler handler) {
        router_.get(pattern, std::move(handler));
        return *this;
    }

    Server& post(std::string_view pattern, Handler handler) {
        router_.post(pattern, std::move(handler));
        return *this;
    }

    Server& put(std::string_view pattern, Handler handler) {
        router_.put(pattern, std::move(handler));
        return *this;
    }

    Server& del(std::string_view pattern, Handler handler) {
        router_.del(pattern, std::move(handler));
        return *this;
    }

    Server& patch(std::string_view pattern, Handler handler) {
        router_.patch(pattern, std::move(handler));
        return *this;
    }

    // Dependency injection / shared application state
    Server& set_state(void* state) noexcept {
        user_state_ = state;
        return *this;
    }

    // Configure listen address and port
    Server& listen(uint16_t port, std::string_view host = "0.0.0.0") {
        port_ = port;
        host_ = std::string(host);
        return *this;
    }

    // Run single-threaded event loop
    void run();

    // Run thread-per-core shared-nothing event loop cluster
    void run(size_t threads);

    void stop();

    [[nodiscard]] const Router& router() const noexcept { return router_; }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }

private:
    core::Task<void> handle_connection(core::EventLoop& loop, int client_fd);
    core::Task<void> handle_http2_connection(core::EventLoop& loop, int client_fd, std::string initial_data);
    core::Task<void> accept_loop(core::EventLoop& loop, int listen_fd);
    int create_listen_socket();

    Router router_;
    std::string host_{"0.0.0.0"};
    uint16_t port_{8080};
    void* user_state_{nullptr};
    std::atomic<bool> running_{false};
    std::vector<std::thread> workers_;
};

} // namespace aegon::http
