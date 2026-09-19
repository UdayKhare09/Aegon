#pragma once

#include "http/Protocol.h"
#include "http/HeaderMap.h"
#include "http/Response.h"
#include "data/validation/Validator.h"
#include <glaze/glaze.hpp>
#include <string_view>
#include <string>
#include <array>
#include <optional>
#include <span>
#include <concepts>

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

    template <typename T>
    [[nodiscard]] std::expected<T, glz::error_ctx> json() const {
        T val{};
        auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(val, body_);
        if (ec) {
            return std::unexpected(ec);
        }
        return val;
    }

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

    /**
     * @brief Parse a single cookie value by name from the inbound "Cookie" header.
     * Zero-allocation and zero-copy.
     *
     * @param key The cookie name to look up
     * @return std::optional<std::string_view> containing the unquoted cookie value, or std::nullopt
     */
    [[nodiscard]] std::optional<std::string_view> cookie(std::string_view key) const noexcept {
        auto raw = header("cookie");
        if (!raw || raw->empty()) return std::nullopt;
        std::string_view c = *raw;
        while (!c.empty()) {
            while (!c.empty() && (c.front() == ' ' || c.front() == '\t')) {
                c.remove_prefix(1);
            }
            if (c.empty()) break;
            size_t semi = c.find(';');
            std::string_view pair = (semi != std::string_view::npos) ? c.substr(0, semi) : c;
            size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                std::string_view k = pair.substr(0, eq);
                while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) {
                    k.remove_suffix(1);
                }
                if (k == key) {
                    std::string_view v = pair.substr(eq + 1);
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
                        v.remove_prefix(1);
                    }
                    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
                        v.remove_suffix(1);
                    }
                    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                        v = v.substr(1, v.size() - 2);
                    }
                    return v;
                }
            }
            if (semi == std::string_view::npos) break;
            c.remove_prefix(semi + 1);
        }
        return std::nullopt;
    }

    /**
     * @brief Binds inbound JSON body into typed DTO T with automatic validation & error response.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> bind_json(Response& res) const {
        T val{};
        auto parse_err = glz::read<glz::opts{.error_on_unknown_keys = false}>(val, body_);
        if (parse_err) {
            struct HttpError {
                int status{400};
                std::string error{"Bad Request"};
                std::string message;
            };
            HttpError err{
                .status = 400,
                .error = "Bad Request",
                .message = "Malformed JSON payload: " + glz::format_error(parse_err, body_)
            };
            std::string err_json;
            std::ignore = glz::write_json(err, err_json);
            res.status(StatusCode::BadRequest).json(err_json);
            return std::nullopt;
        }

        if constexpr (validation::HasValidate<T>) {
            validation::ValidationRules v;
            val.validate(v);
            if (v.has_violations()) {
                res.status(StatusCode::UnprocessableEntity).json(v.to_json());
                return std::nullopt;
            }
        }

        return val;
    }

    /**
     * @brief Binds inbound URL query string into typed DTO T with automatic validation & error response.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> bind_query(Response& res) const {
        glz::generic doc;
        doc.data = glz::generic::object_t{};
        if (!query_.empty()) {
            std::string_view q = query_;
            while (!q.empty()) {
                size_t amp = q.find('&');
                std::string_view pair = (amp != std::string_view::npos) ? q.substr(0, amp) : q;
                size_t eq = pair.find('=');
                if (eq != std::string_view::npos) {
                    std::string k = url_decode_string(pair.substr(0, eq));
                    std::string v = url_decode_string(pair.substr(eq + 1));
                    if (!k.empty()) {
                        if (v == "true") {
                            doc[k] = true;
                        } else if (v == "false") {
                            doc[k] = false;
                        } else if (is_numeric_literal(v)) {
                            if (v.find('.') != std::string_view::npos) {
                                doc[k] = std::strtod(v.c_str(), nullptr);
                            } else {
                                doc[k] = static_cast<int64_t>(std::strtoll(v.c_str(), nullptr, 10));
                            }
                        } else {
                            doc[k] = v;
                        }
                    }
                }
                if (amp == std::string_view::npos) break;
                q.remove_prefix(amp + 1);
            }
        }
        std::string json_doc;
        std::ignore = glz::write_json(doc, json_doc);

        T val{};
        auto parse_err = glz::read<glz::opts{.error_on_unknown_keys = false}>(val, json_doc);
        if (parse_err) {
            struct HttpError {
                int status{400};
                std::string error{"Bad Request"};
                std::string message;
            };
            HttpError err{
                .status = 400,
                .error = "Bad Request",
                .message = "Invalid query parameters: " + glz::format_error(parse_err, json_doc)
            };
            std::string err_json;
            std::ignore = glz::write_json(err, err_json);
            res.status(StatusCode::BadRequest).json(err_json);
            return std::nullopt;
        }

        if constexpr (validation::HasValidate<T>) {
            validation::ValidationRules v;
            val.validate(v);
            if (v.has_violations()) {
                res.status(StatusCode::UnprocessableEntity).json(v.to_json());
                return std::nullopt;
            }
        }

        return val;
    }

    /**
     * @brief Binds route parameters into typed DTO T with automatic validation & error response.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> bind_path(Response& res) const {
        glz::generic doc;
        doc.data = glz::generic::object_t{};
        for (size_t i = 0; i < param_count_; ++i) {
            std::string_view k = params_[i].key;
            std::string_view v = params_[i].value;
            if (v == "true") {
                doc[k] = true;
            } else if (v == "false") {
                doc[k] = false;
            } else if (is_numeric_literal(v)) {
                if (v.find('.') != std::string_view::npos) {
                    doc[k] = std::strtod(std::string(v).c_str(), nullptr);
                } else {
                    doc[k] = static_cast<int64_t>(std::strtoll(std::string(v).c_str(), nullptr, 10));
                }
            } else {
                doc[k] = std::string(v);
            }
        }
        std::string json_doc;
        std::ignore = glz::write_json(doc, json_doc);

        T val{};
        auto parse_err = glz::read<glz::opts{.error_on_unknown_keys = false}>(val, json_doc);
        if (parse_err) {
            struct HttpError {
                int status{400};
                std::string error{"Bad Request"};
                std::string message;
            };
            HttpError err{
                .status = 400,
                .error = "Bad Request",
                .message = "Invalid path parameters: " + glz::format_error(parse_err, json_doc)
            };
            std::string err_json;
            std::ignore = glz::write_json(err, err_json);
            res.status(StatusCode::BadRequest).json(err_json);
            return std::nullopt;
        }

        if constexpr (validation::HasValidate<T>) {
            validation::ValidationRules v;
            val.validate(v);
            if (v.has_violations()) {
                res.status(StatusCode::UnprocessableEntity).json(v.to_json());
                return std::nullopt;
            }
        }

        return val;
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

    static inline bool is_numeric_literal(std::string_view s) noexcept {
        if (s.empty()) return false;
        size_t i = 0;
        if (s[0] == '-' || s[0] == '+') {
            if (s.size() == 1) return false;
            i = 1;
        }
        bool has_dot = false;
        for (; i < s.size(); ++i) {
            if (s[i] == '.') {
                if (has_dot) return false;
                has_dot = true;
            } else if (s[i] < '0' || s[i] > '9') {
                return false;
            }
        }
        return true;
    }

    static inline std::string url_decode_string(std::string_view in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%' && i + 2 < in.size()) {
                auto hex_val = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                int h = hex_val(in[i + 1]);
                int l = hex_val(in[i + 2]);
                if (h >= 0 && l >= 0) {
                    out.push_back(static_cast<char>((h << 4) | l));
                    i += 2;
                    continue;
                }
            } else if (in[i] == '+') {
                out.push_back(' ');
                continue;
            }
            out.push_back(in[i]);
        }
        return out;
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
