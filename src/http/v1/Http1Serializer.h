#pragma once

#include "http/Protocol.h"
#include <string>
#include <string_view>
#include <charconv>

namespace aegon::http {

class Response;

namespace v1 {

/**
 * @brief High-performance HTTP/1.1 wire protocol serializer.
 * Handles fast-path status line generation, header block layout,
 * Content-Length injection, Date header caching, and chunked transfer encoding.
 */
class Http1Serializer {
public:
    /**
     * @brief Serialize a single chunk per RFC 9112 §7.1 (<hex-len>\r\n<data>\r\n)
     */
    static inline void serialize_chunk(std::string_view data, std::string& out) {
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
    static inline void serialize_chunk_end(std::string& out) {
        out.append("0\r\n\r\n");
    }

    /**
     * @brief Append HTTP/1.1 status line and headers (ending in \r\n\r\n) into output buffer.
     */
    static void append_headers(const Response& res, std::string& out);

    /**
     * @brief Clear output buffer and serialize HTTP/1.1 status line and headers.
     */
    static inline void serialize_headers(const Response& res, std::string& out) {
        out.clear();
        append_headers(res, out);
    }

    /**
     * @brief Append complete HTTP/1.1 response (headers + body or chunks) into output buffer without clearing.
     */
    static void append_response(const Response& res, std::string& out);

    /**
     * @brief Clear output buffer and serialize complete HTTP/1.1 response.
     */
    static inline void serialize_response(const Response& res, std::string& out) {
        out.clear();
        append_response(res, out);
    }
};

} // namespace v1
} // namespace aegon::http
