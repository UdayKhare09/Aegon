#pragma once

#include "http/Protocol.h"
#include "http/HeaderMap.h"
#include "data/uuid/UUID.h"
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <charconv>
#include <type_traits>

namespace aegon::http {

class Response {
public:
    Response() = default;

    // Fluent setters
    Response& status(StatusCode code) noexcept {
        status_ = code;
        return *this;
    }

    Response& status(uint16_t code) noexcept {
        status_ = static_cast<StatusCode>(code);
        return *this;
    }

    Response& header(std::string_view name, std::string_view value) {
        headers_.set(name, value);
        return *this;
    }

    Response& set_header_owned(std::string name, std::string value) {
        owned_strings_.push_back(std::move(name));
        owned_strings_.push_back(std::move(value));
        std::string_view v = owned_strings_.back();
        std::string_view n = *(owned_strings_.end() - 2);
        headers_.set(n, v);
        return *this;
    }

    Response& body(std::string b) {
        body_ = std::move(b);
        return *this;
    }

    Response& text(std::string_view t) {
        headers_.set("Content-Type", "text/plain; charset=utf-8");
        body_ = std::string(t);
        return *this;
    }

    Response& json(std::string_view j) {
        headers_.set("Content-Type", "application/json; charset=utf-8");
        body_ = std::string(j);
        return *this;
    }

    template <typename T>
        requires (!std::is_convertible_v<T, std::string_view>)
    Response& json(const T& val) {
        headers_.set("Content-Type", "application/json; charset=utf-8");
        body_.clear();
        (void)glz::write_json(val, body_);
        return *this;
    }

    Response& html(std::string_view h) {
        headers_.set("Content-Type", "text/html; charset=utf-8");
        body_ = std::string(h);
        return *this;
    }

    Response& uuid(const aegon::data::UUID& id) {
        headers_.set("Content-Type", "application/json; charset=utf-8");
        char buf[64];
        std::memcpy(buf, "{\"uuid\":\"", 9);
        id.to_chars(buf + 9);
        std::memcpy(buf + 9 + 36, "\"}", 2);
        body_.assign(buf, 47);
        return *this;
    }

    Response& chunked() {
        is_chunked_ = true;
        headers_.set("Transfer-Encoding", "chunked");
        return *this;
    }

    [[nodiscard]] bool is_chunked() const noexcept { return is_chunked_; }

    // Getters
    [[nodiscard]] StatusCode status() const noexcept { return status_; }
    [[nodiscard]] const HeaderMap& headers() const noexcept { return headers_; }
    [[nodiscard]] HeaderMap& headers() noexcept { return headers_; }
    [[nodiscard]] std::string_view body() const noexcept { return body_; }

    /**
     * @brief Serialize a single chunk per RFC 9112 §7.1 (<hex-len>\r\n<data>\r\n)
     */
    static void serialize_chunk(std::string_view data, std::string& out) {
        if (data.empty()) return;
        char hex_buf[24];
        auto [ptr, _] = std::to_chars(hex_buf, hex_buf + 24, data.size(), 16);
        out.append(hex_buf, ptr - hex_buf);
        out.append("\r\n");
        out.append(data);
        out.append("\r\n");
    }

    /**
     * @brief Serialize terminating chunk per RFC 9112 §7.1 (0\r\n\r\n)
     */
    static void serialize_chunk_end(std::string& out) {
        out.append("0\r\n\r\n");
    }

    /**
     * @brief Serialize complete HTTP/1.1 response into output string buffer.
     */
    void serialize_http1(std::string& out) const {
        out.clear();
        out.reserve(256 + body_.size());

        // Status line: HTTP/1.1 200 OK\r\n
        out.append("HTTP/1.1 ");
        char code_buf[8];
        auto [ptr, _] = std::to_chars(code_buf, code_buf + 8, static_cast<uint16_t>(status_));
        out.append(code_buf, ptr - code_buf);
        out.push_back(' ');
        out.append(status_phrase(status_));
        out.append("\r\n");

        // Content-Length header if body is present and not chunked
        if (is_chunked_) {
            if (!headers_.contains("Transfer-Encoding")) {
                out.append("Transfer-Encoding: chunked\r\n");
            }
        } else if (!headers_.contains("Content-Length")) {
            out.append("Content-Length: ");
            char len_buf[24];
            auto [lptr, unused] = std::to_chars(len_buf, len_buf + 24, body_.size());
            (void)unused;
            out.append(len_buf, lptr - len_buf);
            out.append("\r\n");
        }

        // Custom headers
        for (const auto& h : headers_) {
            out.append(h.name);
            out.append(": ");
            out.append(h.value);
            out.append("\r\n");
        }

        out.append("\r\n");

        if (is_chunked_) {
            if (!body_.empty()) {
                serialize_chunk(body_, out);
            }
            serialize_chunk_end(out);
        } else {
            out.append(body_);
        }
    }

private:
    StatusCode status_{StatusCode::Ok};
    HeaderMap headers_{};
    std::string body_{};
    bool is_chunked_{false};
    std::vector<std::string> owned_strings_{};
};

} // namespace aegon::http
