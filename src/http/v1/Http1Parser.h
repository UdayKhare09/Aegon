#pragma once

#include "http/Request.h"
#include <string_view>
#include <cstdint>
#include <cstring>
#include <string>

namespace aegon::http::v1 {

enum class ParseStatus {
    Complete,
    NeedMoreData,
    Error,
    NotImplemented
};

class Http1Parser {
public:
    /**
     * @brief Parse hex chunk size per RFC 9112 §7.1
     */
    static bool parse_hex_size(std::string_view hex_str, size_t& size) noexcept {
        if (hex_str.empty()) return false;
        size = 0;
        for (char c : hex_str) {
            size <<= 4;
            if (c >= '0' && c <= '9') {
                size += (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                size += (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                size += (c - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Parse and decode HTTP/1.1 request from buffer with full RFC 9112 compliance.
     * @param buffer Raw packet data
     * @param req Target Request object to populate
     * @param bytes_consumed Output number of bytes processed
     */
    static ParseStatus parse(std::string_view buffer, Request& req, size_t& bytes_consumed) noexcept {
        bytes_consumed = 0;

        // 1. Locate Request Line (\r\n)
        size_t req_line_end = buffer.find("\r\n");
        if (req_line_end == std::string_view::npos) {
            return ParseStatus::NeedMoreData;
        }

        std::string_view req_line = buffer.substr(0, req_line_end);

        // Parse method
        size_t sp1 = req_line.find(' ');
        if (sp1 == std::string_view::npos) return ParseStatus::Error;
        std::string_view method_sv = req_line.substr(0, sp1);
        Method m = string_to_method(method_sv);
        if (m == Method::UNKNOWN) return ParseStatus::Error;
        req.set_method(m);

        // Parse target URI & query string
        size_t sp2 = req_line.find(' ', sp1 + 1);
        if (sp2 == std::string_view::npos) return ParseStatus::Error;
        std::string_view full_path = req_line.substr(sp1 + 1, sp2 - (sp1 + 1));
        if (full_path.empty() || full_path[0] != '/') {
            // Asterisk form for OPTIONS or absoluteURI
            if (full_path != "*" && !full_path.starts_with("http://") && !full_path.starts_with("https://")) {
                return ParseStatus::Error;
            }
        }

        size_t qmark = full_path.find('?');
        if (qmark != std::string_view::npos) {
            req.set_path(full_path.substr(0, qmark));
            req.set_query(full_path.substr(qmark + 1));
        } else {
            req.set_path(full_path);
            req.set_query("");
        }

        // Parse HTTP version
        std::string_view ver_sv = req_line.substr(sp2 + 1);
        if (ver_sv == "HTTP/1.1") {
            req.set_version(HttpVersion::Http1_1);
        } else if (ver_sv == "HTTP/1.0") {
            req.set_version(HttpVersion::Http1_0);
        } else {
            return ParseStatus::Error;
        }

        // 2. Parse Headers
        size_t cursor = req_line_end + 2;
        req.headers().clear();
        req.clear_params();
        req.set_expect_continue(false);
        req.set_upgrade_h2c(false);

        size_t content_length = 0;
        bool has_content_length = false;
        bool has_transfer_encoding = false;
        bool is_chunked = false;
        bool has_host = false;

        while (cursor < buffer.size()) {
            // Check for end of headers (\r\n)
            if (buffer.size() >= cursor + 2 && buffer[cursor] == '\r' && buffer[cursor + 1] == '\n') {
                cursor += 2;
                break;
            }

            size_t header_end = buffer.find("\r\n", cursor);
            if (header_end == std::string_view::npos) {
                return ParseStatus::NeedMoreData;
            }

            std::string_view line = buffer.substr(cursor, header_end - cursor);
            size_t colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0) {
                return ParseStatus::Error; // Missing colon or empty field-name
            }

            std::string_view name = line.substr(0, colon);
            // RFC 9110: No whitespace allowed between header field name and colon
            if (name.back() == ' ' || name.back() == '\t') {
                return ParseStatus::Error;
            }

            std::string_view value = line.substr(colon + 1);
            // Trim leading whitespace
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            // Trim trailing whitespace
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                value.remove_suffix(1);
            }

            req.headers().add(name, value);

            if (iequals(name, "Host")) {
                if (has_host) {
                    return ParseStatus::Error; // Multiple Host headers forbidden (RFC 9112 §3.2)
                }
                has_host = true;
            } else if (iequals(name, "Content-Length")) {
                if (has_content_length) {
                    return ParseStatus::Error; // Duplicate Content-Length forbidden
                }
                has_content_length = true;
                content_length = 0;
                for (char c : value) {
                    if (c >= '0' && c <= '9') {
                        content_length = content_length * 10 + (c - '0');
                    } else {
                        return ParseStatus::Error; // Invalid non-digit in Content-Length
                    }
                }
            } else if (iequals(name, "Transfer-Encoding")) {
                has_transfer_encoding = true;
                if (iequals(value, "chunked")) {
                    is_chunked = true;
                } else {
                    return ParseStatus::NotImplemented; // Only chunked transfer encoding is supported
                }
            } else if (iequals(name, "Expect")) {
                if (iequals(value, "100-continue")) {
                    req.set_expect_continue(true);
                }
            } else if (iequals(name, "Upgrade")) {
                if (iequals(value, "h2c")) {
                    req.set_upgrade_h2c(true);
                }
            }

            cursor = header_end + 2;
        }

        // RFC 9112 §3.2: HTTP/1.1 requires Host header
        if (req.version() == HttpVersion::Http1_1 && !has_host) {
            return ParseStatus::Error;
        }

        // RFC 9112 §6.1: Request Smuggling Protection
        // Mutual exclusion of Content-Length and Transfer-Encoding
        if (has_content_length && has_transfer_encoding) {
            return ParseStatus::Error;
        }

        // 3. Body Parsing
        if (is_chunked) {
            // RFC 9112 §7.1: Chunked Transfer Decoding
            std::string decoded_body;
            size_t chunk_cursor = cursor;

            while (true) {
                size_t line_end = buffer.find("\r\n", chunk_cursor);
                if (line_end == std::string_view::npos) {
                    return ParseStatus::NeedMoreData;
                }

                std::string_view size_line = buffer.substr(chunk_cursor, line_end - chunk_cursor);
                // Strip chunk-ext if present (e.g. "4;foo=bar")
                size_t semi = size_line.find(';');
                if (semi != std::string_view::npos) {
                    size_line = size_line.substr(0, semi);
                }

                size_t chunk_size = 0;
                if (!parse_hex_size(size_line, chunk_size)) {
                    return ParseStatus::Error;
                }

                chunk_cursor = line_end + 2;

                if (chunk_size == 0) {
                    // Last-chunk reached. Now consume trailer section up to \r\n\r\n
                    size_t trailer_end = buffer.find("\r\n\r\n", chunk_cursor);
                    if (trailer_end == std::string_view::npos) {
                        // Check if immediate \r\n (empty trailers)
                        if (buffer.size() >= chunk_cursor + 2 &&
                            buffer[chunk_cursor] == '\r' && buffer[chunk_cursor + 1] == '\n') {
                            chunk_cursor += 2;
                        } else {
                            return ParseStatus::NeedMoreData;
                        }
                    } else {
                        chunk_cursor = trailer_end + 4;
                    }
                    cursor = chunk_cursor;
                    break;
                }

                // Verify buffer has complete chunk + \r\n
                if (buffer.size() < chunk_cursor + chunk_size + 2) {
                    return ParseStatus::NeedMoreData;
                }

                // Verify chunk ends with \r\n
                if (buffer[chunk_cursor + chunk_size] != '\r' || buffer[chunk_cursor + chunk_size + 1] != '\n') {
                    return ParseStatus::Error;
                }

                decoded_body.append(buffer.data() + chunk_cursor, chunk_size);
                chunk_cursor += chunk_size + 2;
            }

            req.set_decoded_body(std::move(decoded_body));
        } else if (has_content_length && content_length > 0) {
            size_t remaining = buffer.size() - cursor;
            if (remaining < content_length) {
                return ParseStatus::NeedMoreData;
            }
            req.set_body(buffer.substr(cursor, content_length));
            cursor += content_length;
        } else {
            req.set_body("");
        }

        bytes_consumed = cursor;
        return ParseStatus::Complete;
    }
};

} // namespace aegon::http::v1
