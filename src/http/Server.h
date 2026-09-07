#pragma once

#include "http/Router.h"
#include "http/tls/TlsContext.h"
#include "http/v3/Http3Server.h"
#include "core/EventLoop.h"
#include "core/Task.h"
#include "data/orm/sql/SqlConfig.h"
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

namespace aegon::data::orm::sql {
class SqlDatabaseClient;
class PerCoreConnectionPool;
}

namespace aegon::http {

namespace v3 {
class Http3Server;
}

class Server;

/**
 * @brief Fluent multi-database server configuration namespace (e.g. server.db.sql({...})).
 */
struct ServerDatabaseConfig {
    Server& server;
    explicit ServerDatabaseConfig(Server& s) : server(s) {}

    Server& sql(const data::orm::sql::SqlConfig& config);
    Server& sql(data::orm::sql::SqlDatabaseClient* client);
};

class Server {
public:
    // Multi-database configuration namespace
    ServerDatabaseConfig db{*this};

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

    // Dependency injection / shared application state
    Server& set_state(void* state) noexcept {
        user_state_ = state;
        return *this;
    }

    // Enable TLS (HTTPS) with ALPN (h2 and http/1.1)
    Server& enable_tls(const std::string& cert_file = "", const std::string& key_file = "");

    // Configure listen address and port
    Server& listen(uint16_t port, std::string_view host = "0.0.0.0") {
        port_ = port;
        host_ = std::string(host);
        return *this;
    }

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
    [[nodiscard]] data::orm::sql::SqlDatabaseClient* sql_client() const noexcept { return sql_client_; }

private:
    core::Task<void> handle_connection(core::EventLoop& loop, int client_fd);
    core::Task<void> handle_http2_connection(core::EventLoop& loop, int client_fd, std::string initial_data);
    core::Task<void> handle_http2_upgrade(core::EventLoop& loop, int client_fd, Request req, std::string http2_settings, std::string initial_data);
    core::Task<void> handle_tls_connection(core::EventLoop& loop, int client_fd);
    core::Task<void> accept_loop(core::EventLoop& loop, int listen_fd);
    int create_listen_socket();

    Router router_;
    std::string host_{"0.0.0.0"};
    uint16_t port_{8080};
    void* user_state_{nullptr};
    std::atomic<bool> running_{false};
    std::vector<std::thread> workers_;

    bool tls_enabled_{false};
    bool http3_enabled_{true}; // Default to enabled when TLS is used
    std::unique_ptr<tls::TlsContext> tls_ctx_;
    std::unique_ptr<v3::Http3Server> h3_server_;

    // SQL Connection Pool and Client
    std::unique_ptr<data::orm::sql::PerCoreConnectionPool> sql_pool_;
    std::unique_ptr<data::orm::sql::SqlDatabaseClient> owned_sql_client_;
    data::orm::sql::SqlDatabaseClient* sql_client_{nullptr};

    friend struct ServerDatabaseConfig;
};

} // namespace aegon::http
