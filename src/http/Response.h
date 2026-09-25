#pragma once

#include "http/Protocol.h"
#include "http/HeaderMap.h"
#include "http/Cookie.h"
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <deque>
#include <charconv>
#include <type_traits>
#include <sys/stat.h>

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

    Response& version(HttpVersion v) noexcept {
        version_ = v;
        return *this;
    }

    [[nodiscard]] HttpVersion version() const noexcept { return version_; }

    Response& header(std::string_view name, std::string_view value) {
        headers_.set(name, value);
        return *this;
    }

    Response& set_header_owned(std::string name, std::string value) {
        owned_strings_.push_back(std::move(name));
        std::string_view n = owned_strings_.back();
        owned_strings_.push_back(std::move(value));
        std::string_view v = owned_strings_.back();
        headers_.set(n, v);
        return *this;
    }

    /**
     * @brief Set an HTTP cookie using modern C++ designated initializers.
     * Appends a Set-Cookie header per RFC 6265. Supports multiple cookies per response.
     */
    Response& set_cookie(const CookieOptions& options) {
        std::string cookie_str = format_cookie(options);
        owned_strings_.push_back(std::move(cookie_str));
        headers_.add("Set-Cookie", owned_strings_.back());
        return *this;
    }

    /**
     * @brief Clear an HTTP cookie on the client by expiring it immediately (Max-Age=0).
     */
    Response& clear_cookie(std::string_view name, std::string_view path = "/", std::string_view domain = {}) {
        return set_cookie({
            .name = name,
            .value = "",
            .path = path,
            .domain = domain,
            .max_age = std::chrono::seconds(0)
        });
    }

    Response& body(std::string b) {
        body_ = std::move(b);
        return *this;
    }

    Response& text(std::string_view t) {
        headers_.set("Content-Type", "text/plain");
        body_ = std::string(t);
        return *this;
    }

    Response& json(std::string_view j) {
        headers_.set("Content-Type", "application/json");
        body_ = std::string(j);
        return *this;
    }

    template <typename T>
        requires (!std::is_convertible_v<T, std::string_view>)
    Response& json(const T& val) {
        headers_.set("Content-Type", "application/json");
        body_.clear();
        (void)glz::write_json(val, body_);
        return *this;
    }

    Response& html(std::string_view h) {
        headers_.set("Content-Type", "text/html; charset=utf-8");
        body_ = std::string(h);
        return *this;
    }

    static std::string_view infer_mime_type(std::string_view path) noexcept {
        auto dot = path.rfind('.');
        if (dot == std::string_view::npos) return "application/octet-stream";
        std::string_view ext = path.substr(dot);
        if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
        if (ext == ".css") return "text/css; charset=utf-8";
        if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=utf-8";
        if (ext == ".json") return "application/json; charset=utf-8";
        if (ext == ".png") return "image/png";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".gif") return "image/gif";
        if (ext == ".svg") return "image/svg+xml";
        if (ext == ".ico") return "image/x-icon";
        if (ext == ".txt") return "text/plain; charset=utf-8";
        if (ext == ".pdf") return "application/pdf";
        if (ext == ".wasm") return "application/wasm";
        if (ext == ".webp") return "image/webp";
        if (ext == ".woff2") return "font/woff2";
        if (ext == ".xml") return "application/xml; charset=utf-8";
        return "application/octet-stream";
    }

    Response& file(const std::string& filepath, std::string_view mime_type = "") {
        is_file_ = true;
        file_path_ = filepath;
        struct stat st{};
        if (::stat(filepath.c_str(), &st) == 0) {
            file_size_ = static_cast<size_t>(st.st_size);
        } else {
            file_size_ = 0;
            status_ = StatusCode::NotFound;
        }

        if (mime_type.empty()) {
            mime_type = infer_mime_type(filepath);
        }
        headers_.set("Content-Type", mime_type);
        return *this;
    }

    [[nodiscard]] bool has_file() const noexcept { return is_file_; }
    [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
    [[nodiscard]] size_t file_size() const noexcept { return file_size_; }


    Response& chunked() {
        is_chunked_ = true;
        headers_.set("Transfer-Encoding", "chunked");
        return *this;
    }

    [[nodiscard]] bool is_chunked() const noexcept { return is_chunked_; }

    // Getters
    [[nodiscard]] StatusCode status() const noexcept { return status_; }
    [[nodiscard]] uint16_t status_code() const noexcept { return static_cast<uint16_t>(status_); }
    [[nodiscard]] bool is_success() const noexcept {
        auto s = static_cast<uint16_t>(status_);
        return s >= 200 && s <= 299;
    }
    [[nodiscard]] const HeaderMap& headers() const noexcept { return headers_; }
    [[nodiscard]] HeaderMap& headers() noexcept { return headers_; }
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
     * @brief Append only the status line and headers (ending in \r\n\r\n) to existing buffer.
     */
    void append_http1_headers(std::string& out) const {
        out.reserve(out.size() + 256);

        // Fast status line
        if (status_ == StatusCode::Ok) {
            out.append("HTTP/1.1 200 OK\r\n");
        } else if (status_ == StatusCode::NotFound) {
            out.append("HTTP/1.1 404 Not Found\r\n");
        } else if (status_ == StatusCode::InternalServerError) {
            out.append("HTTP/1.1 500 Internal Server Error\r\n");
        } else {
            out.append("HTTP/1.1 ");
            char code_buf[8];
            auto [ptr, _] = std::to_chars(code_buf, code_buf + 8, static_cast<uint16_t>(status_));
            out.append(code_buf, ptr - code_buf);
            out.push_back(' ');
            out.append(status_phrase(status_));
            out.append("\r\n");
        }

        uint16_t sc = static_cast<uint16_t>(status_);
        bool no_content_body = (sc >= 100 && sc < 200) || sc == 204 || sc == 304;

        if (is_chunked_) {
            if (!headers_.contains("Transfer-Encoding")) {
                out.append("Transfer-Encoding: chunked\r\n");
            }
        } else if (!no_content_body && !headers_.contains("Content-Length")) {
            out.append("Content-Length: ");
            size_t len = is_file_ ? file_size_ : body_.size();
            if (len < 10) {
                out.push_back(static_cast<char>('0' + len));
                out.append("\r\n");
            } else {
                char len_buf[24];
                auto [lptr, unused] = std::to_chars(len_buf, len_buf + 24, len);
                (void)unused;
                out.append(len_buf, lptr - len_buf);
                out.append("\r\n");
            }
        }

        for (const auto& h : headers_) {
            out.append(h.name);
            out.append(": ");
            out.append(h.value);
            out.append("\r\n");
        }

        out.append("\r\n");
    }

    /**
     * @brief Serialize only the status line and headers (ending in \r\n\r\n).
     */
    void serialize_http1_headers(std::string& out) const {
        out.clear();
        append_http1_headers(out);
    }

    /**
     * @brief Append complete HTTP/1.1 response into output string buffer without clearing.
     */
    void append_http1(std::string& out) const {
        append_http1_headers(out);

        if (is_chunked_) {
            if (!body_.empty()) {
                serialize_chunk(body_, out);
            }
            serialize_chunk_end(out);
        } else if (!is_file_) {
            out.append(body_);
        }
    }

    /**
     * @brief Serialize complete HTTP/1.1 response into output string buffer.
     */
    void serialize_http1(std::string& out) const {
        out.clear();
        append_http1(out);
    }

private:
    HttpVersion version_{HttpVersion::Http1_1};
    StatusCode status_{StatusCode::Ok};
    HeaderMap headers_{};
    std::string body_{};
    bool is_chunked_{false};
    bool is_file_{false};
    std::string file_path_{};
    size_t file_size_{0};
    std::deque<std::string> owned_strings_{};
};

} // namespace aegon::http
