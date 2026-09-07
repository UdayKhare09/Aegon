#pragma once

#include "http/Protocol.h"
#include "http/HeaderMap.h"
#include "http/Response.h"
#include "data/uuid/UUID.h"
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

    /**
     * @brief Binds inbound JSON body into typed DTO T with automatic validation & error response.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> bind_json(Response& res) const {
        T val{};
        auto parse_err = glz::read<glz::opts{.error_on_unknown_keys = false}>(val, body_);
        if (parse_err) {
            std::string err_desc = glz::format_error(parse_err, body_);
            std::string err_json = "{\"status\":400,\"error\":\"Bad Request\",\"message\":\"Malformed JSON payload: ";
            for (char c : err_desc) {
                if (c == '"') err_json += "\\\"";
                else if (c == '\\') err_json += "\\\\";
                else if (c == '\n') err_json += " ";
                else if (c == '\r') continue;
                else err_json += c;
            }
            err_json += "\"}";
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
        std::string json_doc = "{";
        if (!query_.empty()) {
            std::string_view q = query_;
            bool first = true;
            while (!q.empty()) {
                size_t amp = q.find('&');
                std::string_view pair = (amp != std::string_view::npos) ? q.substr(0, amp) : q;
                size_t eq = pair.find('=');
                if (eq != std::string_view::npos) {
                    std::string k = url_decode_string(pair.substr(0, eq));
                    std::string v = url_decode_string(pair.substr(eq + 1));
                    if (!k.empty()) {
                        if (!first) json_doc.push_back(',');
                        first = false;
                        append_escaped_json(k, json_doc);
                        json_doc.push_back(':');
                        if (v == "true" || v == "false") {
                            json_doc.append(v);
                        } else if (is_numeric_literal(v)) {
                            json_doc.append(v);
                        } else {
                            append_escaped_json(v, json_doc);
                        }
                    }
                }
                if (amp == std::string_view::npos) break;
                q.remove_prefix(amp + 1);
            }
        }
        json_doc.push_back('}');

        T val{};
        auto parse_err = glz::read<glz::opts{.error_on_unknown_keys = false}>(val, json_doc);
        if (parse_err) {
            std::string err_desc = glz::format_error(parse_err, json_doc);
            std::string err_json = "{\"status\":400,\"error\":\"Bad Request\",\"message\":\"Invalid query parameters: ";
            for (char c : err_desc) {
                if (c == '"') err_json += "\\\"";
                else if (c == '\\') err_json += "\\\\";
                else if (c == '\n') err_json += " ";
                else if (c == '\r') continue;
                else err_json += c;
            }
            err_json += "\"}";
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
        std::string json_doc = "{";
        bool first = true;
        for (size_t i = 0; i < param_count_; ++i) {
            if (!first) json_doc.push_back(',');
            first = false;
            append_escaped_json(params_[i].key, json_doc);
            json_doc.push_back(':');
            std::string_view v = params_[i].value;
            if (v == "true" || v == "false") {
                json_doc.append(v);
            } else if (is_numeric_literal(v)) {
                json_doc.append(v);
            } else {
                append_escaped_json(v, json_doc);
            }
        }
        json_doc.push_back('}');

        T val{};
        auto parse_err = glz::read<glz::opts{.error_on_unknown_keys = false}>(val, json_doc);
        if (parse_err) {
            std::string err_desc = glz::format_error(parse_err, json_doc);
            std::string err_json = "{\"status\":400,\"error\":\"Bad Request\",\"message\":\"Invalid path parameters: ";
            for (char c : err_desc) {
                if (c == '"') err_json += "\\\"";
                else if (c == '\\') err_json += "\\\\";
                else if (c == '\n') err_json += " ";
                else if (c == '\r') continue;
                else err_json += c;
            }
            err_json += "\"}";
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
    static inline void append_escaped_json(std::string_view val, std::string& out) {
        out.push_back('"');
        for (char c : val) {
            if (c == '"') out.append("\\\"");
            else if (c == '\\') out.append("\\\\");
            else if (c == '\n') out.append("\\n");
            else if (c == '\r') out.append("\\r");
            else if (c == '\t') out.append("\\t");
            else out.push_back(c);
        }
        out.push_back('"');
    }

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
