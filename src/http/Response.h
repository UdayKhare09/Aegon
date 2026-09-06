#pragma once

#include "http/Protocol.h"
#include "http/HeaderMap.h"
#include "data/uuid/UUID.h"
#include <string>
#include <string_view>
#include <charconv>

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

    // Getters
    [[nodiscard]] StatusCode status() const noexcept { return status_; }
    [[nodiscard]] const HeaderMap& headers() const noexcept { return headers_; }
    [[nodiscard]] HeaderMap& headers() noexcept { return headers_; }
    [[nodiscard]] std::string_view body() const noexcept { return body_; }

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

        // Content-Length header if body is present
        if (!headers_.contains("Content-Length")) {
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
        out.append(body_);
    }

private:
    StatusCode status_{StatusCode::Ok};
    HeaderMap headers_{};
    std::string body_{};
};

} // namespace aegon::http
