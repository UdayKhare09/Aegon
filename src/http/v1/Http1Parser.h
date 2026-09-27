#pragma once

#include "http/Request.h"
#include "core/simd/SimdString.h"
#include <string_view>
#include <cstdint>
#include <cstring>
#include <string>
#include <charconv>

namespace aegon::http::v1 {

enum class ParseStatus {
    Complete,
    NeedMoreData,
    Error,
    NotImplemented,
    UriTooLong,
    HeadersTooLarge,
    PayloadTooLarge,
    ExpectationFailed
};

class Http1Parser {
public:
    static constexpr size_t MAX_URI_LENGTH = aegon::http::MAX_URI_LENGTH;
    static constexpr size_t MAX_HEADERS_SIZE = aegon::http::MAX_HEADERS_SIZE;
    static constexpr bool is_tchar(char c) noexcept {
        unsigned char uc = static_cast<unsigned char>(c);
        if ((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9')) {
            return true;
        }
        return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
               c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
               c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
    }

    /**
     * @brief Parse hex chunk size per RFC 9112 §7.1 using branchless lookup table
     */
    static bool parse_hex_size(std::string_view hex_str, size_t& size) noexcept {
        if (hex_str.empty() || hex_str.size() > 16) return false;

        static constexpr auto HEX_TABLE = []() consteval {
            std::array<uint8_t, 256> table{};
            table.fill(0xFF);
            for (uint8_t i = 0; i <= 9; ++i) table[static_cast<size_t>('0' + i)] = i;
            for (uint8_t i = 0; i < 6; ++i) {
                table[static_cast<size_t>('a' + i)] = static_cast<uint8_t>(10 + i);
                table[static_cast<size_t>('A' + i)] = static_cast<uint8_t>(10 + i);
            }
            return table;
        }();

        size = 0;
        for (char c : hex_str) {
            uint8_t val = HEX_TABLE[static_cast<uint8_t>(c)];
            if (val == 0xFF) return false;
            size = (size << 4) | val;
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
        size_t req_line_end = aegon::core::simd::SimdString::find_crlf(buffer);
        if (req_line_end == std::string_view::npos) {
            return ParseStatus::NeedMoreData;
        }

        std::string_view req_line = buffer.substr(0, req_line_end);

        // Parse method
        size_t sp1 = aegon::core::simd::SimdString::find_char(req_line, ' ');
        if (sp1 == std::string_view::npos) return ParseStatus::Error;
        std::string_view method_sv = req_line.substr(0, sp1);
        Method m = string_to_method(method_sv);
        if (m == Method::UNKNOWN) return ParseStatus::Error;
        req.set_method(m);

        // Parse target URI & query string
        size_t sp2 = aegon::core::simd::SimdString::find_char(req_line, ' ', sp1 + 1);
        if (sp2 == std::string_view::npos) return ParseStatus::Error;
        std::string_view full_path = req_line.substr(sp1 + 1, sp2 - (sp1 + 1));
        if (full_path.size() > MAX_URI_LENGTH) {
            return ParseStatus::UriTooLong;
        }
        if (full_path.empty() || full_path[0] != '/') [[unlikely]] {
            // Asterisk form for OPTIONS or absoluteURI per RFC 9112 §3.2.2
            if (full_path == "*") {
                if (m != Method::OPTIONS) return ParseStatus::Error;
            } else if (full_path.starts_with("http://")) {
                full_path.remove_prefix(7);
                size_t slash = aegon::core::simd::SimdString::find_char(full_path, '/');
                full_path = (slash != std::string_view::npos) ? full_path.substr(slash) : "/";
            } else if (full_path.starts_with("https://")) {
                full_path.remove_prefix(8);
                size_t slash = aegon::core::simd::SimdString::find_char(full_path, '/');
                full_path = (slash != std::string_view::npos) ? full_path.substr(slash) : "/";
            } else {
                return ParseStatus::Error;
            }
        }

        // RFC 9112 §3.2 / RFC 3986: request-target validation (no fragments, no poison %00, no CRLF)
        if (!is_valid_request_target(full_path)) {
            return ParseStatus::Error;
        }

        // Must be ASCII without CTLs, NUL, or high bytes
        for (char c : full_path) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc <= 0x20 || uc >= 0x7F) {
                return ParseStatus::Error;
            }
        }

        size_t qmark = aegon::core::simd::SimdString::find_char(full_path, '?');
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
        } else if (ver_sv.starts_with("HTTP/1.") && ver_sv.size() >= 8) {
            bool digits = true;
            for (size_t i = 7; i < ver_sv.size(); ++i) {
                if (ver_sv[i] < '0' || ver_sv[i] > '9') { digits = false; break; }
            }
            if (digits) {
                req.set_version(HttpVersion::Http1_1);
            } else {
                return ParseStatus::Error;
            }
        } else {
            return ParseStatus::Error;
        }

        // 2. Parse Headers
        size_t cursor = req_line_end + 2;
        req.headers().clear();
        req.clear_params();
        req.set_expect_continue(false);
        req.set_upgrade_h2c(false);
        req.set_websocket_upgrade(false);
        req.set_sec_websocket_key({});

        size_t content_length = 0;
        bool has_content_length = false;
        bool has_transfer_encoding = false;
        bool is_chunked = false;
        bool has_host = false;

        bool headers_complete = false;

        while (cursor < buffer.size()) {
            if (cursor - req_line_end > MAX_HEADERS_SIZE) {
                return ParseStatus::HeadersTooLarge;
            }
            // Check for end of headers (\r\n)
            if (buffer.size() >= cursor + 2 && buffer[cursor] == '\r' && buffer[cursor + 1] == '\n') {
                cursor += 2;
                headers_complete = true;
                break;
            }
            if (buffer[cursor] == '\r' && cursor + 1 == buffer.size()) {
                return ParseStatus::NeedMoreData;
            }

            size_t header_end = aegon::core::simd::SimdString::find_crlf(buffer, cursor);
            if (header_end == std::string_view::npos) {
                if (buffer.size() - req_line_end > MAX_HEADERS_SIZE) {
                    return ParseStatus::HeadersTooLarge;
                }
                return ParseStatus::NeedMoreData;
            }

            std::string_view line = buffer.substr(cursor, header_end - cursor);
            size_t colon = aegon::core::simd::SimdString::find_char(line, ':');
            if (colon == std::string_view::npos || colon == 0) {
                return ParseStatus::Error; // Missing colon or empty field-name
            }

            std::string_view name = line.substr(0, colon);
            // RFC 9110 §5.6.2: Header name must consist only of tchar
            for (char c : name) {
                if (!is_tchar(c)) return ParseStatus::Error;
            }

            // RFC 9110: No whitespace allowed between header field name and colon
            if (name.back() == ' ' || name.back() == '\t') {
                return ParseStatus::Error;
            }

            std::string_view raw_value = line.substr(colon + 1);
            // RFC 9112 §2.2 / RFC 9110 §5.5: Reject bare CR, NUL, and CTLs (except HTAB)
            for (char c : raw_value) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (uc == '\r' || (uc < 0x20 && uc != '\t') || uc == 0x7F) {
                    return ParseStatus::Error;
                }
            }

            std::string_view value = raw_value;
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
                if (value.empty()) {
                    return ParseStatus::Error; // Empty Host header forbidden
                }
                for (char c : value) {
                    if (c == '@' || c == '/' || c == ',' || c == ' ' || c == '\t') {
                        return ParseStatus::Error;
                    }
                }
            } else if (iequals(name, "Content-Length")) {
                if (has_content_length) {
                    return ParseStatus::Error; // Duplicate Content-Length forbidden
                }
                has_content_length = true;
                auto cl_opt = parse_valid_content_length(value);
                if (!cl_opt) {
                    return ParseStatus::Error;
                }
                content_length = *cl_opt;
                if (content_length > MAX_BODY_SIZE) {
                    return ParseStatus::PayloadTooLarge;
                }
            } else if (iequals(name, "Transfer-Encoding")) {
                if (has_transfer_encoding) {
                    // Duplicate Transfer-Encoding header -> RFC 9112 ambiguous -> 400
                    return ParseStatus::Error;
                }
                has_transfer_encoding = true;
                if (value.empty()) {
                    return ParseStatus::Error; // Empty value -> 400
                }

                // Parse comma-separated codings
                std::string_view te_val = value;
                std::vector<std::string_view> codings;
                while (!te_val.empty()) {
                    size_t comma = te_val.find(',');
                    std::string_view token = (comma != std::string_view::npos) ? te_val.substr(0, comma) : te_val;
                    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.remove_prefix(1);
                    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.remove_suffix(1);
                    if (!token.empty()) {
                        codings.push_back(token);
                    }
                    if (comma == std::string_view::npos) break;
                    te_val.remove_prefix(comma + 1);
                }

                if (codings.empty()) {
                    return ParseStatus::Error;
                }

                size_t chunked_count = 0;
                for (auto c : codings) {
                    if (iequals(c, "chunked") || c.starts_with("chunked;")) {
                        ++chunked_count;
                    }
                }
                if (chunked_count > 1) {
                    return ParseStatus::Error;
                }

                std::string_view final_coding = codings.back();
                if (iequals(final_coding, "chunked") || final_coding.starts_with("chunked;")) {
                    is_chunked = true;
                } else {
                    return ParseStatus::NotImplemented;
                }
            } else if (iequals(name, "Expect")) {
                if (iequals(value, "100-continue")) {
                    req.set_expect_continue(true);
                } else {
                    return ParseStatus::ExpectationFailed;
                }
            } else if (iequals(name, "Upgrade")) {
                if (iequals(value, "h2c")) {
                    req.set_upgrade_h2c(true);
                } else {
                    std::string lower_val;
                    lower_val.reserve(value.size());
                    for (char ch : value) lower_val.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                    if (lower_val.find("websocket") != std::string::npos) {
                        req.set_websocket_upgrade(true);
                    }
                }
            } else if (iequals(name, "Sec-WebSocket-Key")) {
                req.set_sec_websocket_key(value);
            }

            cursor = header_end + 2;
        }

        if (!headers_complete) {
            return ParseStatus::NeedMoreData;
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
                size_t line_end = aegon::core::simd::SimdString::find_crlf(buffer, chunk_cursor);
                if (line_end == std::string_view::npos) {
                    size_t lf = buffer.find('\n', chunk_cursor);
                    if (lf != std::string_view::npos) {
                        return ParseStatus::Error;
                    }
                    return ParseStatus::NeedMoreData;
                }

                std::string_view size_line = buffer.substr(chunk_cursor, line_end - chunk_cursor);
                // Strip and validate chunk-ext if present (e.g. "4;foo=bar")
                size_t semi = aegon::core::simd::SimdString::find_char(size_line, ';');
                if (semi != std::string_view::npos) {
                    std::string_view ext = size_line.substr(semi + 1);
                    if (ext.empty() || ext.size() > 1024) {
                        return ParseStatus::Error;
                    }
                    size_t ext_pos = 0;
                    while (ext_pos < ext.size()) {
                        size_t eq_or_semi = ext.find_first_of(";=", ext_pos);
                        std::string_view ext_name = ext.substr(ext_pos, eq_or_semi == std::string_view::npos ? ext.size() - ext_pos : eq_or_semi - ext_pos);
                        while (!ext_name.empty() && (ext_name.front() == ' ' || ext_name.front() == '\t')) ext_name.remove_prefix(1);
                        while (!ext_name.empty() && (ext_name.back() == ' ' || ext_name.back() == '\t')) ext_name.remove_suffix(1);
                        if (ext_name.empty()) {
                            return ParseStatus::Error;
                        }
                        for (char c : ext_name) {
                            if (!is_tchar(c)) return ParseStatus::Error;
                        }
                        if (eq_or_semi == std::string_view::npos || ext[eq_or_semi] == ';') {
                            ext_pos = (eq_or_semi == std::string_view::npos) ? ext.size() : eq_or_semi + 1;
                        } else {
                            size_t next_semi = ext.find(';', eq_or_semi + 1);
                            std::string_view ext_val = ext.substr(eq_or_semi + 1, next_semi == std::string_view::npos ? ext.size() - (eq_or_semi + 1) : next_semi - (eq_or_semi + 1));
                            for (char c : ext_val) {
                                unsigned char uc = static_cast<unsigned char>(c);
                                if (uc == '\r' || uc < 0x20 || uc == 0x7F) {
                                    return ParseStatus::Error;
                                }
                            }
                            ext_pos = (next_semi == std::string_view::npos) ? ext.size() : next_semi + 1;
                        }
                    }
                    size_line = size_line.substr(0, semi);
                }

                size_t chunk_size = 0;
                if (!parse_hex_size(size_line, chunk_size)) {
                    return ParseStatus::Error;
                }

                chunk_cursor = line_end + 2;

                if (chunk_size == 0) {
                    if (buffer.size() > chunk_cursor && buffer[chunk_cursor] == '\n') {
                        return ParseStatus::Error;
                    }
                    // Last-chunk reached. Now consume trailer section up to \r\n\r\n
                    size_t trailer_end = aegon::core::simd::SimdString::find_double_crlf(buffer, chunk_cursor);
                    if (trailer_end == std::string_view::npos) {
                        // Check if immediate \r\n (empty trailers)
                        if (buffer.size() >= chunk_cursor + 2 &&
                            buffer[chunk_cursor] == '\r' && buffer[chunk_cursor + 1] == '\n') {
                            chunk_cursor += 2;
                        } else {
                            size_t lf = buffer.find('\n', chunk_cursor);
                            if (lf != std::string_view::npos && (lf == chunk_cursor || buffer[lf - 1] != '\r')) {
                                return ParseStatus::Error;
                            }
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

                if (decoded_body.size() + chunk_size > MAX_BODY_SIZE) {
                    return ParseStatus::PayloadTooLarge;
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

    /**
     * @brief Parse and decode an HTTP/1.1 response from buffer (RFC 9112).
     * @param buffer Raw packet data received from server
     * @param res Target Response object to populate
     * @param bytes_consumed Output number of bytes processed
     */
    static ParseStatus parse_response(std::string_view buffer, Response& res, size_t& bytes_consumed) noexcept {
        bytes_consumed = 0;

        // 1. Locate Status Line (\r\n)
        size_t status_line_end = aegon::core::simd::SimdString::find_crlf(buffer);
        if (status_line_end == std::string_view::npos) {
            return ParseStatus::NeedMoreData;
        }

        std::string_view status_line = buffer.substr(0, status_line_end);

        // Format: HTTP/1.1 <status_code> [reason phrase]
        size_t sp1 = aegon::core::simd::SimdString::find_char(status_line, ' ');
        if (sp1 == std::string_view::npos) return ParseStatus::Error;

        std::string_view proto_sv = status_line.substr(0, sp1);
        if (proto_sv == "HTTP/1.1") {
            res.version(HttpVersion::Http1_1);
        } else if (proto_sv == "HTTP/1.0") {
            res.version(HttpVersion::Http1_0);
        } else if (proto_sv == "HTTP/2.0" || proto_sv == "HTTP/2") {
            res.version(HttpVersion::Http2);
        } else if (proto_sv == "HTTP/3.0" || proto_sv == "HTTP/3") {
            res.version(HttpVersion::Http3);
        }

        size_t sp2 = aegon::core::simd::SimdString::find_char(status_line, ' ', sp1 + 1);
        std::string_view code_sv = (sp2 == std::string_view::npos)
            ? status_line.substr(sp1 + 1)
            : status_line.substr(sp1 + 1, sp2 - sp1 - 1);

        uint16_t status_code = 0;
        auto [ptr, ec] = std::from_chars(code_sv.data(), code_sv.data() + code_sv.size(), status_code);
        if (ec != std::errc{} || status_code < 100 || status_code > 599) {
            return ParseStatus::Error;
        }
        res.status(status_code);

        // 2. Locate Header Termination (\r\n\r\n)
        size_t headers_end = aegon::core::simd::SimdString::find_double_crlf(buffer);
        if (headers_end == std::string_view::npos) {
            return ParseStatus::NeedMoreData;
        }

        // 3. Parse Headers
        size_t cursor = status_line_end + 2;
        bool is_chunked = false;
        size_t content_length = 0;
        bool has_content_length = false;

        while (cursor < headers_end) {
            size_t line_end = aegon::core::simd::SimdString::find_crlf(buffer, cursor);
            if (line_end == std::string_view::npos || line_end > headers_end) {
                break;
            }

            std::string_view line = buffer.substr(cursor, line_end - cursor);
            cursor = line_end + 2;

            if (line.empty()) continue;

            size_t colon = aegon::core::simd::SimdString::find_char(line, ':');
            if (colon == std::string_view::npos || colon == 0) {
                return ParseStatus::Error;
            }

            std::string_view name = line.substr(0, colon);
            std::string_view value = line.substr(colon + 1);

            // Trim leading/trailing whitespace
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                value.remove_suffix(1);
            }

            res.header(name, value);

            // Check Content-Length & Transfer-Encoding
            if (name.size() == 14) {
                bool match = true;
                const char* cl = "content-length";
                for (size_t i = 0; i < 14; ++i) {
                    char c = name[i];
                    if (c >= 'A' && c <= 'Z') c += 32;
                    if (c != cl[i]) { match = false; break; }
                }
                if (match) {
                    has_content_length = true;
                    auto [cptr, cec] = std::from_chars(value.data(), value.data() + value.size(), content_length);
                    if (cec != std::errc{}) return ParseStatus::Error;
                }
            } else if (name.size() == 17) {
                bool match = true;
                const char* te = "transfer-encoding";
                for (size_t i = 0; i < 17; ++i) {
                    char c = name[i];
                    if (c >= 'A' && c <= 'Z') c += 32;
                    if (c != te[i]) { match = false; break; }
                }
                if (match && value.find("chunked") != std::string_view::npos) {
                    is_chunked = true;
                }
            }
        }

        cursor = headers_end + 4; // Skip \r\n\r\n

        // Responses to HEAD or 204 No Content / 304 Not Modified have no body
        if (status_code == 204 || status_code == 304 || (status_code >= 100 && status_code < 200)) {
            res.body("");
            bytes_consumed = cursor;
            return ParseStatus::Complete;
        }

        // 4. Parse Body
        if (is_chunked) {
            std::string decoded_body;
            size_t chunk_cursor = cursor;

            while (true) {
                size_t line_end = buffer.find("\r\n", chunk_cursor);
                if (line_end == std::string_view::npos) {
                    return ParseStatus::NeedMoreData;
                }

                std::string_view size_line = buffer.substr(chunk_cursor, line_end - chunk_cursor);
                size_t semi = size_line.find(';');
                if (semi != std::string_view::npos) {
                    std::string_view ext = size_line.substr(semi + 1);
                    if (ext.empty() || ext.size() > 1024) {
                        return ParseStatus::Error;
                    }
                    size_line = size_line.substr(0, semi);
                }

                size_t chunk_size = 0;
                if (!parse_hex_size(size_line, chunk_size)) {
                    return ParseStatus::Error;
                }

                chunk_cursor = line_end + 2;

                if (chunk_size == 0) {
                    size_t trailer_end = buffer.find("\r\n\r\n", chunk_cursor);
                    if (trailer_end == std::string_view::npos) {
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

                if (buffer.size() < chunk_cursor + chunk_size + 2) {
                    return ParseStatus::NeedMoreData;
                }

                if (buffer[chunk_cursor + chunk_size] != '\r' || buffer[chunk_cursor + chunk_size + 1] != '\n') {
                    return ParseStatus::Error;
                }

                decoded_body.append(buffer.data() + chunk_cursor, chunk_size);
                chunk_cursor += chunk_size + 2;
            }

            res.body(std::move(decoded_body));
        } else if (has_content_length) {
            size_t remaining = buffer.size() - cursor;
            if (remaining < content_length) {
                return ParseStatus::NeedMoreData;
            }
            res.body(std::string(buffer.substr(cursor, content_length)));
            cursor += content_length;
        } else {
            res.body("");
        }

        bytes_consumed = cursor;
        return ParseStatus::Complete;
    }
};

} // namespace aegon::http::v1
