#pragma once

#include "http/Request.h"
#include <string_view>
#include <cstdint>
#include <cstring>

namespace aegon::http::v1 {

enum class ParseStatus {
    Complete,
    NeedMoreData,
    Error
};

class Http1Parser {
public:
    /**
     * @brief Zero-copy parse HTTP/1.1 request from buffer.
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
        req.set_method(string_to_method(method_sv));

        // Parse target URL & query string
        size_t sp2 = req_line.find(' ', sp1 + 1);
        if (sp2 == std::string_view::npos) return ParseStatus::Error;
        std::string_view full_path = req_line.substr(sp1 + 1, sp2 - (sp1 + 1));

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
            req.set_version(HttpVersion::Http1_1);
        }

        // 2. Parse Headers
        size_t cursor = req_line_end + 2;
        req.headers().clear();
        req.clear_params();

        size_t content_length = 0;

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
            if (colon != std::string_view::npos) {
                std::string_view name = line.substr(0, colon);
                std::string_view value = line.substr(colon + 1);
                // Trim leading whitespace from value
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                    value.remove_prefix(1);
                }
                req.headers().add(name, value);

                if (iequals(name, "Content-Length")) {
                    content_length = 0;
                    for (char c : value) {
                        if (c >= '0' && c <= '9') {
                            content_length = content_length * 10 + (c - '0');
                        } else {
                            break;
                        }
                    }
                }
            }

            cursor = header_end + 2;
        }

        // 3. Parse Body if Content-Length > 0
        if (content_length > 0) {
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
