#pragma once

#include "http/Protocol.h"
#include "http/Response.h"
#include "http/jwt/JwtAlgorithm.h"
#include "core/Task.h"
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <chrono>
#include <memory>
#include <optional>

namespace aegon::http::client {

class HttpClient;

struct RequestHeader {
    std::string name;
    std::string value;
};

struct RequestState {
    HttpClient& client;
    Method method{Method::GET};
    std::string url{};
    std::vector<RequestHeader> headers{};
    std::string body{};
    std::vector<std::string> cookies{};
    std::chrono::milliseconds timeout{30000};
    HttpVersion version{HttpVersion::Http1_1};
    bool follow_redirects{true};
    uint8_t retries{0};
    std::chrono::milliseconds retry_backoff{100};

    RequestState(HttpClient& c, Method m, std::string u);
};

/**
 * @brief Fluent, chainable HTTP request builder.
 */
class RequestBuilder {
public:
    RequestBuilder(HttpClient& client, Method method, std::string url);

    // URL & Query
    RequestBuilder& query(std::string_view key, std::string_view value);
    RequestBuilder& query(const std::vector<std::pair<std::string, std::string>>& params);
    RequestBuilder& query(std::string_view raw_query);

    // Headers & Auth
    RequestBuilder& header(std::string_view name, std::string_view value);
    RequestBuilder& bearer_auth(std::string_view token);
    RequestBuilder& basic_auth(std::string_view username, std::string_view password);
    RequestBuilder& api_key(std::string_view header_name, std::string_view key);
    RequestBuilder& content_type(std::string_view mime);
    RequestBuilder& accept(std::string_view mime);
    RequestBuilder& user_agent(std::string_view ua);

    // Cookies
    RequestBuilder& cookie(std::string_view name, std::string_view value);

    // Body
    RequestBuilder& body(std::string b);
    RequestBuilder& body(std::string_view b);
    RequestBuilder& body(std::span<const uint8_t> bytes);

    template <typename T>
        requires (!std::is_convertible_v<T, std::string_view> && !std::is_convertible_v<T, std::span<const uint8_t>>)
    RequestBuilder& json(const T& val) {
        header("Content-Type", "application/json; charset=utf-8");
        state_->body.clear();
        (void)glz::write_json(val, state_->body);
        return *this;
    }

    RequestBuilder& form(const std::vector<std::pair<std::string, std::string>>& fields);

    // Configuration overrides
    RequestBuilder& version(HttpVersion v);
    RequestBuilder& http2() { return version(HttpVersion::Http2); }
    RequestBuilder& http3() { return version(HttpVersion::Http3); }
    RequestBuilder& timeout(std::chrono::milliseconds ms);
    RequestBuilder& follow_redirects(bool follow);
    RequestBuilder& retry(uint8_t count, std::chrono::milliseconds backoff = std::chrono::milliseconds(100));

    // Execute
    core::Task<Response> send();
    Response send_sync();

    // Getters
    [[nodiscard]] Method method() const noexcept { return state_->method; }
    [[nodiscard]] const std::string& url() const noexcept { return state_->url; }
    [[nodiscard]] HttpVersion version() const noexcept { return state_->version; }
    [[nodiscard]] const std::vector<RequestHeader>& headers() const noexcept { return state_->headers; }
    [[nodiscard]] const std::string& body() const noexcept { return state_->body; }
    [[nodiscard]] std::chrono::milliseconds timeout() const noexcept { return state_->timeout; }
    [[nodiscard]] bool follow_redirects() const noexcept { return state_->follow_redirects; }
    [[nodiscard]] uint8_t retries() const noexcept { return state_->retries; }
    [[nodiscard]] std::chrono::milliseconds retry_backoff() const noexcept { return state_->retry_backoff; }
    [[nodiscard]] const std::shared_ptr<RequestState>& state() const noexcept { return state_; }

private:
    std::shared_ptr<RequestState> state_;
};

} // namespace aegon::http::client
