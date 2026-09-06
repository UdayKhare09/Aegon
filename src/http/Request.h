#pragma once

#include "http/Protocol.h"
#include "http/HeaderMap.h"
#include "data/uuid/UUID.h"
#include <string_view>
#include <array>
#include <optional>
#include <span>

namespace aegon::http {

struct RouteParam {
    std::string_view key;
    std::string_view value;
};

class Request {
public:
    static constexpr size_t MAX_ROUTE_PARAMS = 8;

    Request() = default;

    // Setters
    void set_version(HttpVersion v) noexcept { version_ = v; }
    void set_method(Method m) noexcept { method_ = m; }
    void set_path(std::string_view p) noexcept { path_ = p; }
    void set_query(std::string_view q) noexcept { query_ = q; }
    void set_body(std::string_view b) noexcept { body_ = b; }

    // Header access
    HeaderMap& headers() noexcept { return headers_; }
    const HeaderMap& headers() const noexcept { return headers_; }

    // Getters
    [[nodiscard]] HttpVersion version() const noexcept { return version_; }
    [[nodiscard]] Method method() const noexcept { return method_; }
    [[nodiscard]] std::string_view path() const noexcept { return path_; }
    [[nodiscard]] std::string_view query() const noexcept { return query_; }
    [[nodiscard]] std::string_view body() const noexcept { return body_; }

    // Route parameters (:id, :username, etc.)
    void add_param(std::string_view key, std::string_view value) noexcept {
        if (param_count_ < MAX_ROUTE_PARAMS) {
            params_[param_count_++] = {key, value};
        }
    }

    void clear_params() noexcept {
        param_count_ = 0;
    }

    [[nodiscard]] std::optional<std::string_view> param(std::string_view key) const noexcept {
        for (size_t i = 0; i < param_count_; ++i) {
            if (params_[i].key == key) {
                return params_[i].value;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Zero-copy parse route parameter directly into a SIMD-validated 128-bit UUID.
     */
    [[nodiscard]] std::optional<aegon::data::UUID> param_uuid(std::string_view key) const noexcept {
        auto val = param(key);
        if (!val.has_value()) return std::nullopt;
        return aegon::data::UUID::from_string(*val);
    }

    /**
     * @brief Parse query parameter from query string (e.g. ?foo=bar&baz=123)
     */
    [[nodiscard]] std::optional<std::string_view> query_param(std::string_view key) const noexcept {
        if (query_.empty()) return std::nullopt;
        std::string_view q = query_;
        while (!q.empty()) {
            size_t amp = q.find('&');
            std::string_view pair = (amp != std::string_view::npos) ? q.substr(0, amp) : q;
            size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                std::string_view k = pair.substr(0, eq);
                std::string_view v = pair.substr(eq + 1);
                if (k == key) return v;
            } else if (pair == key) {
                return "";
            }
            if (amp == std::string_view::npos) break;
            q.remove_prefix(amp + 1);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string_view> query(std::string_view key) const noexcept {
        return query_param(key);
    }

    [[nodiscard]] std::optional<std::string_view> header(std::string_view key) const noexcept {
        return headers_.get(key);
    }

    // RFC compliance helpers
    [[nodiscard]] bool expect_continue() const noexcept { return expect_continue_; }
    void set_expect_continue(bool ec) noexcept { expect_continue_ = ec; }

    [[nodiscard]] bool is_upgrade_h2c() const noexcept { return is_upgrade_h2c_; }
    void set_upgrade_h2c(bool u) noexcept { is_upgrade_h2c_ = u; }

    void set_decoded_body(std::string body) {
        decoded_body_storage_ = std::move(body);
        body_ = decoded_body_storage_;
    }

private:
    HttpVersion version_{HttpVersion::Http1_1};
    Method method_{Method::GET};
    std::string_view path_{"/"};
    std::string_view query_{};
    std::string_view body_{};
    std::string decoded_body_storage_{};
    HeaderMap headers_{};
    bool expect_continue_{false};
    bool is_upgrade_h2c_{false};

    std::array<RouteParam, MAX_ROUTE_PARAMS> params_{};
    size_t param_count_{0};
};

} // namespace aegon::http
