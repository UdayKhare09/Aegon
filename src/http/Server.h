#pragma once

#include "http/Router.h"
#include "http/ServiceRegistry.h"
#include "http/tls/TlsContext.h"
#include "http/v3/Http3Server.h"
#include "core/EventLoop.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

namespace aegon::http {

namespace v3 {
class Http3Server;
}

class Server {
public:
    Server();
    explicit Server(Router router);
    ~Server();

    Server(Server&&) noexcept;
    Server& operator=(Server&&) noexcept;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /**
     * @brief Sets or updates the Router instance.
     */
    Server& set_router(Router router) {
        router_ = std::move(router);
        return *this;
    }

    [[nodiscard]] const Router& router() const noexcept { return router_; }
    [[nodiscard]] Router& router() noexcept { return router_; }

    /**
     * @brief Appends global middleware to the server router.
     */
    template <typename F>
    Server& use(F&& middleware) {
        router_.use(std::forward<F>(middleware));
        return *this;
    }

    [[nodiscard]] RouteGroup group(std::string_view prefix) {
        return router_.group(prefix);
    }

    template <typename F>
    Server& all(std::string_view pattern, F&& handler) {
        router_.all(pattern, std::forward<F>(handler));
        return *this;
    }

    template <typename F>
    Server& all(std::string_view pattern, std::vector<MiddlewareFn> middlewares, F&& handler) {
        router_.all(pattern, std::move(middlewares), std::forward<F>(handler));
        return *this;
    }

    template <typename ClusterT, typename OptionsT>
    Server& proxy(std::string_view pattern,
                  ClusterT cluster,
                  OptionsT options,
                  std::vector<MiddlewareFn> middlewares = {}) {
        router_.proxy(pattern, std::move(cluster), std::move(options), std::move(middlewares));
        return *this;
    }

    template <typename F>
    Server& set_error_handler(F&& handler) {
        router_.set_error_handler(std::forward<F>(handler));
        return *this;
    }

    template <typename F>
    Server& set_not_found_handler(F&& handler) {
        router_.set_not_found_handler(std::forward<F>(handler));
        return *this;
    }

    template <typename F>
    Server& set_method_not_allowed_handler(F&& handler) {
        router_.set_method_not_allowed_handler(std::forward<F>(handler));
        return *this;
    }

    /**
     * @brief Registers a shared service (database client, redis, or custom domain service) in the ServiceRegistry.
     */
    template <typename T>
    Server& provide(std::shared_ptr<T> service) {
        services_->register_service<T>(std::move(service));
        return *this;
    }

    /**
     * @brief Registers a named shared service in the ServiceRegistry.
     */
    template <typename T>
    Server& provide(std::string_view name, std::shared_ptr<T> service) {
        services_->register_service<T>(name, std::move(service));
        return *this;
    }

    /**
     * @brief Instantiates and registers a service of type T in the ServiceRegistry.
     */
    template <typename T, typename... Args>
        requires (!std::is_convertible_v<std::tuple_element_t<0, std::tuple<Args..., void>>, std::string_view>)
    Server& provide(Args&&... args) {
        services_->register_service<T>(std::make_shared<T>(std::forward<Args>(args)...));
        return *this;
    }

    /**
     * @brief Instantiates and registers a named service of type T in the ServiceRegistry.
     */
    template <typename T, typename... Args>
    Server& provide_named(std::string_view name, Args&&... args) {
        services_->register_service<T>(name, std::make_shared<T>(std::forward<Args>(args)...));
        return *this;
    }

    /**
     * @brief Freezes the ServiceRegistry, activating the zero-lock hot path for all handler lookups.
     */
    Server& freeze_services() {
        services_->freeze();
        return *this;
    }

    /**
     * @brief Backward-compatible alias to register typed application state in ServiceRegistry.
     */
    template <typename T>
    Server& set_state(std::shared_ptr<T> state) {
        return provide<T>(std::move(state));
    }

    [[nodiscard]] ServiceRegistry& services() noexcept { return *services_; }
    [[nodiscard]] const ServiceRegistry& services() const noexcept { return *services_; }

    template <typename T>
    [[nodiscard]] std::shared_ptr<T> service(std::string_view name = "") const {
        return services_->get_shared<T>(name);
    }

    using LifecycleHook = std::function<core::Task<void>(Server&)>;
    using BackgroundWorker = std::function<core::Task<void>(Server&, core::EventLoop&)>;

    /**
     * @brief Registers an asynchronous startup hook executed before accepting traffic.
     * Ideal for schema migrations, seed data, cache pre-warming, health checks, and service announcements.
     */
    Server& on_start(LifecycleHook hook) {
        startup_hooks_.push_back(std::move(hook));
        return *this;
    }

    /**
     * @brief Registers an asynchronous shutdown hook executed during server stop.
     * Ideal for queue draining, cache flushing, and service deregistration.
     */
    Server& on_stop(LifecycleHook hook) {
        shutdown_hooks_.push_back(std::move(hook));
        return *this;
    }

    /**
     * @brief Registers a long-running background worker coroutine spawned on the server event loop.
     * Ideal for Redis stream consumers, event workers, and metrics collectors.
     */
    Server& spawn_worker(BackgroundWorker worker) {
        background_workers_.push_back(std::move(worker));
        return *this;
    }

    // Enable TLS (HTTPS) with ALPN (h2 and http/1.1)
    Server& enable_tls(const std::string& cert_file = "", const std::string& key_file = "");

    // Configure listen address and port
    Server& listen(uint16_t port, std::string_view host = "0.0.0.0") {
        if (port == 0) {
            throw std::runtime_error("Server configuration error: port must be greater than 0 (1-65535)");
        }
        port_ = port;
        host_ = std::string(host);
        return *this;
    }

    // Configure SQPOLL (dedicated kernel submission thread)
    Server& enable_sqpoll(bool enable = true, uint32_t idle_ms = 2000, int cpu = -1) noexcept {
        sqpoll_enabled_ = enable;
        sq_thread_idle_ms_ = idle_ms;
        sq_thread_cpu_ = cpu;
        return *this;
    }

    Server& ring_entries(uint32_t entries) noexcept {
        ring_entries_ = entries;
        return *this;
    }

    Server& buffer_pool_entries(uint16_t entries) noexcept {
        buffer_pool_entries_ = entries;
        return *this;
    }

    [[nodiscard]] uint16_t buffer_pool_entries() const noexcept { return buffer_pool_entries_; }

    // Enable/disable HTTP/3 over QUIC
    Server& enable_http3(bool enable = true) noexcept {
        http3_enabled_ = enable;
        return *this;
    }

    // Run single-threaded event loop
    void run();

    // Run thread-per-core shared-nothing event loop cluster
    void run(size_t threads);

    // Stop server
    void stop();

    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] std::string_view host() const noexcept { return host_; }
    [[nodiscard]] bool is_tls_enabled() const noexcept { return tls_enabled_; }
    [[nodiscard]] bool is_http3_enabled() const noexcept { return http3_enabled_; }
    [[nodiscard]] bool is_sqpoll_enabled() const noexcept { return sqpoll_enabled_; }

private:
    core::Task<void> handle_connection(core::EventLoop& loop, int client_fd);
    core::Task<void> handle_http2_connection(core::EventLoop& loop, int client_fd, std::string initial_data);
    core::Task<void> handle_http2_upgrade(core::EventLoop& loop, int client_fd, Request req, std::string http2_settings, std::string initial_data);
    core::Task<void> handle_tls_connection(core::EventLoop& loop, int client_fd);
    core::Task<void> accept_loop(core::EventLoop& loop, int listen_fd);
    core::Task<bool> stream_file_zero_copy(core::EventLoop& loop, int client_fd, const std::string& file_path, size_t file_size);
    int create_listen_socket();

    Router router_;
    std::string host_{"0.0.0.0"};
    uint16_t port_{8080};
    std::shared_ptr<ServiceRegistry> services_{std::make_shared<ServiceRegistry>()};
    std::vector<LifecycleHook> startup_hooks_;
    std::vector<LifecycleHook> shutdown_hooks_;
    std::vector<BackgroundWorker> background_workers_;
    std::atomic<bool> running_{false};
    std::vector<std::thread> workers_;

    bool sqpoll_enabled_{false};
    uint32_t sq_thread_idle_ms_{2000};
    int sq_thread_cpu_{-1};
    uint32_t ring_entries_{4096};
    uint16_t buffer_pool_entries_{8192};

    bool tls_enabled_{false};
    bool http3_enabled_{true}; // Default to enabled when TLS is used
    std::unique_ptr<tls::TlsContext> tls_ctx_;
    std::unique_ptr<v3::Http3Server> h3_server_;
};

} // namespace aegon::http
