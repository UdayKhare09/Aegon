#include "http/v1/Http1Serializer.h"
#include "http/Response.h"

namespace aegon::http::v1 {

void Http1Serializer::append_headers(const Response& res, std::string& out) {
    if (out.capacity() - out.size() < 256) [[unlikely]] {
        out.reserve(std::max(out.capacity() * 2, out.size() + 512));
    }

    const auto& headers = res.headers();

    // Fast-path Tier 1: Standard 200 OK response with 0 linear header scans
    if (res.status() == StatusCode::Ok && !res.is_chunked() && !res.is_sse() && !res.has_file()) [[likely]] {
        std::string_view b = res.body();
        size_t needed = 128 + b.size();
        for (const auto& h : headers) {
            needed += h.name.size() + h.value.size() + 4;
        }
        if (out.capacity() - out.size() < needed) [[unlikely]] {
            out.reserve(std::max(out.capacity() * 2, out.size() + needed + 512));
        }

        out.append("HTTP/1.1 200 OK\r\n");
        out.append(get_http_date_header());

        bool has_cl = false;
        bool has_date = false;
        for (const auto& h : headers) {
            if (iequals(h.name, "Content-Length")) has_cl = true;
            else if (iequals(h.name, "Date")) has_date = true;
            out.append(h.name);
            out.append(": ");
            out.append(h.value);
            out.append("\r\n");
        }

        if (!has_cl) {
            out.append("Content-Length: ");
            size_t len = b.size();
            if (len < 10) {
                out.push_back(static_cast<char>('0' + len));
                out.append("\r\n\r\n");
            } else {
                char len_buf[24];
                auto [lptr, _] = std::to_chars(len_buf, len_buf + 24, len);
                out.append(len_buf, lptr - len_buf);
                out.append("\r\n\r\n");
            }
        } else {
            out.append("\r\n");
        }
        return;
    }

    // Fast status line lookup for other status codes
    switch (res.status()) {
        case StatusCode::Ok:
            out.append("HTTP/1.1 200 OK\r\n");
            break;
        case StatusCode::Created:
            out.append("HTTP/1.1 201 Created\r\n");
            break;
        case StatusCode::NoContent:
            out.append("HTTP/1.1 204 No Content\r\n");
            break;
        case StatusCode::NotModified:
            out.append("HTTP/1.1 304 Not Modified\r\n");
            break;
        case StatusCode::BadRequest:
            out.append("HTTP/1.1 400 Bad Request\r\n");
            break;
        case StatusCode::NotFound:
            out.append("HTTP/1.1 404 Not Found\r\n");
            break;
        case StatusCode::InternalServerError:
            out.append("HTTP/1.1 500 Internal Server Error\r\n");
            break;
        default: {
            out.append("HTTP/1.1 ");
            char code_buf[8];
            auto [ptr, _] = std::to_chars(code_buf, code_buf + 8, static_cast<uint16_t>(res.status()));
            out.append(code_buf, ptr - code_buf);
            out.push_back(' ');
            out.append(status_phrase(res.status()));
            out.append("\r\n");
            break;
        }
    }

    bool no_content_body = should_suppress_content_length(res.status());

    if (res.is_sse()) {
        // SSE over H1: chunked framing, no Content-Length
        if (!headers.contains("Transfer-Encoding")) {
            out.append("Transfer-Encoding: chunked\r\n");
        }
    } else if (res.is_chunked()) {
        if (!headers.contains("Transfer-Encoding")) {
            out.append("Transfer-Encoding: chunked\r\n");
        }
    } else if (!no_content_body && !headers.contains("Content-Length")) {
        out.append("Content-Length: ");
        size_t len = res.has_file() ? res.file_size() : res.body().size();
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

    if (!headers.contains("Date") && static_cast<uint16_t>(res.status()) >= 200) {
        out.append(get_http_date_header());
    }

    for (const auto& h : headers) {
        out.append(h.name);
        out.append(": ");
        out.append(h.value);
        out.append("\r\n");
    }

    out.append("\r\n");
}

void Http1Serializer::append_response(const Response& res, std::string& out) {
    append_headers(res, out);

    if (res.is_chunked()) {
        if (!res.body().empty()) {
            serialize_chunk(res.body(), out);
        }
        serialize_chunk_end(out);
    } else if (!res.has_file()) {
        out.append(res.body());
    }
}

} // namespace aegon::http::v1
