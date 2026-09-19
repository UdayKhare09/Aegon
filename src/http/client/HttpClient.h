#pragma once

#include "http/client/Url.h"
#include "http/client/ConnectionPool.h"
#include "http/client/RequestBuilder.h"
#include "http/Response.h"
#include "core/Task.h"

#include <string>
#include <string_view>
#include <memory>
#include <chrono>

namespace aegon::http::client {

struct ClientConfig {
    HttpVersion default_protocol{HttpVersion::Http1_1};
    std::chrono::milliseconds timeout{30000};
    std::chrono::milliseconds connect_timeout{5000};
    size_t max_connections_per_host{32};
    size_t max_idle_connections{128};
    std::chrono::seconds idle_timeout{60};
    bool follow_redirects{true};
    uint8_t max_redirects{10};
    std::string user_agent{"Aegon-HttpClient/1.0"};
    TlsClientOptions tls{};
};

class HttpClient {
public:
    explicit HttpClient(ClientConfig config = {});
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    // HTTP Verb Factories
    [[nodiscard]] RequestBuilder get(std::string_view url);
    [[nodiscard]] RequestBuilder post(std::string_view url);
    [[nodiscard]] RequestBuilder put(std::string_view url);
    [[nodiscard]] RequestBuilder patch(std::string_view url);
    [[nodiscard]] RequestBuilder del(std::string_view url);
    [[nodiscard]] RequestBuilder head(std::string_view url);
    [[nodiscard]] RequestBuilder options(std::string_view url);
    [[nodiscard]] RequestBuilder request(Method method, std::string_view url);

    void close();

    core::Task<Response> execute_single(std::shared_ptr<RequestState> state, core::EventLoop& loop);
    core::Task<Response> execute(std::shared_ptr<RequestState> state, core::EventLoop& loop);
    core::Task<Response> execute(const RequestBuilder& req, core::EventLoop& loop);

    [[nodiscard]] ConnectionPool& pool() noexcept { return pool_; }
    [[nodiscard]] const ClientConfig& config() const noexcept { return config_; }

private:
    ClientConfig config_{};
    ConnectionPool pool_;
};

} // namespace aegon::http::client
