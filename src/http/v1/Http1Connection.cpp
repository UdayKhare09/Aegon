#include "http/v1/Http1Connection.h"
#include "http/v2/Http2Frame.h"
#include "http/websocket/WebSocketHandshake.h"
#include "http/websocket/WebSocketConnection.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <algorithm>

namespace aegon::http::v1 {

Http1Connection::Http1Connection(core::EventLoop& loop, int client_fd, const Router& router,
                                 const ServiceRegistry* services, const ServerConfig& config)
    : loop_(loop), client_fd_(client_fd), router_(router), services_(services), config_(config) {}

std::optional<Response> Http1Connection::make_parse_error_response(ParseStatus status) {
    Response res;
    res.header("Connection", "close");
    switch (status) {
        case ParseStatus::UriTooLong:
            res.status(StatusCode::UriTooLong).text("URI Too Long: request URI exceeds limit");
            return res;
        case ParseStatus::HeadersTooLarge:
            res.status(StatusCode::RequestHeaderFieldsTooLarge).text("Request Header Fields Too Large");
            return res;
        case ParseStatus::PayloadTooLarge:
            res.status(StatusCode::PayloadTooLarge).text("Payload Too Large: maximum body size exceeded");
            return res;
        case ParseStatus::ExpectationFailed:
            res.status(StatusCode::ExpectationFailed).text("Expectation Failed");
            return res;
        case ParseStatus::Error:
            res.status(StatusCode::BadRequest).text("Bad Request");
            return res;
        case ParseStatus::NotImplemented:
            res.status(StatusCode::NotImplemented).text("Not Implemented");
            return res;
        default:
            return std::nullopt;
    }
}

bool Http1Connection::evaluate_keep_alive(const Request& req, Response& res) {
    bool keep_alive = true;
    if (auto conn_hdr = req.headers().get("Connection")) {
        if (core::simd::SimdString::iequals(*conn_hdr, "close")) {
            keep_alive = false;
        }
    } else if (req.version() == HttpVersion::Http1_0) {
        keep_alive = false;
    }
    if (!keep_alive) {
        res.header("Connection", "close");
    }
    return keep_alive;
}

std::string Http1Connection::build_alt_svc_header(uint16_t port, bool http3_enabled) {
    return http3_enabled
        ? ("h3=\":" + std::to_string(port) + "\"; ma=86400, h2=\":" + std::to_string(port) + "\"; ma=86400")
        : ("h2=\":" + std::to_string(port) + "\"; ma=86400");
}

std::optional<Response> Http1Connection::validate_websocket_upgrade(const Request& req) {
    auto ws_ver = req.headers().get("Sec-WebSocket-Version");
    if (!ws_ver || *ws_ver != "13") {
        Response ver_res;
        ver_res.status(StatusCode::UpgradeRequired)
               .header("Sec-WebSocket-Version", "13")
               .text("Upgrade Required: Sec-WebSocket-Version 13 required");
        return ver_res;
    }
    return std::nullopt;
}

core::Task<bool> Http1Connection::stream_file_zero_copy(core::EventLoop& loop, int client_fd,
                                                        const std::string& file_path, size_t file_size) {
    if (file_size == 0) co_return true;

    int file_fd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) co_return false;

    int pipefd[2];
    if (::pipe2(pipefd, O_NONBLOCK | O_CLOEXEC) < 0) {
        (void)(co_await loop.ring().close(file_fd));
        co_return false;
    }

    int64_t in_off = 0;
    size_t remaining = file_size;
    constexpr unsigned int CHUNK_SIZE = 32768;
    bool ok = true;

    while (remaining > 0) {
        unsigned int to_splice = static_cast<unsigned int>(std::min<size_t>(remaining, CHUNK_SIZE));
        // 1. Splice file -> pipe[1] (disk page cache to kernel pipe buffer)
        int n1 = co_await loop.ring().splice(file_fd, in_off, pipefd[1], -1, to_splice, 0);
        if (n1 <= 0) {
            ok = false;
            break;
        }
        in_off += n1;

        // 2. Splice pipe[0] -> socket_fd (kernel pipe buffer to network socket buffer)
        int n2 = co_await loop.ring().splice(pipefd[0], -1, client_fd, -1, static_cast<unsigned int>(n1), 0);
        if (n2 <= 0) {
            ok = false;
            break;
        }
        remaining -= static_cast<size_t>(n2);
    }

    ::close(pipefd[0]);
    ::close(pipefd[1]);
    (void)(co_await loop.ring().close(file_fd));
    co_return ok;
}

core::Task<void> Http1Connection::run(H2DirectCallback on_h2_direct,
                                      H2UpgradeCallback on_h2_upgrade) {
    std::string req_accum;
    req_accum.reserve(4096);
    std::string resp_batch;
    resp_batch.reserve(4096);
    bool first_packet = true;

    while (true) {
        auto recv_res = co_await loop_.ring().recv_provided(client_fd_, loop_.buffer_pool().bgid());
        if (recv_res.bytes == -ENOBUFS) {
            co_await loop_.ring().timeout(100'000ULL);
            continue;
        }
        if (recv_res.bytes <= 0) {
            break;
        }

        auto buf_slice = loop_.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        req_accum.append(reinterpret_cast<const char*>(buf_slice.data()), buf_slice.size());
        loop_.buffer_pool().return_buffer(recv_res.bid);

        bool keep_alive = true;
        size_t req_offset = 0;

        while (req_offset < req_accum.size()) {
            std::string_view unparsed(req_accum.data() + req_offset, req_accum.size() - req_offset);
            if (first_packet) {
                if (unparsed.starts_with(v2::CLIENT_PREFACE)) {
                    first_packet = false;
                    if (req_offset > 0) {
                        req_accum.erase(0, req_offset);
                    }
                    if (on_h2_direct) {
                        co_await on_h2_direct(client_fd_, std::move(req_accum));
                    }
                    co_return;
                }
                first_packet = false;
            }

            Request req;
            size_t bytes_consumed = 0;
            auto status = Http1Parser::parse(unparsed, req, bytes_consumed, config_.limits);

            if (status == ParseStatus::NeedMoreData) {
                if (req.expect_continue()) {
                    req.set_expect_continue(false);
                    (void)(co_await loop_.ring().send_all(client_fd_, "HTTP/1.1 100 Continue\r\n\r\n"));
                }
                break;
            }

            if (auto err_res = make_parse_error_response(status)) {
                std::string out;
                Http1Serializer::serialize_response(*err_res, out);
                (void)(co_await loop_.ring().send_all(client_fd_, out));
                keep_alive = false;
                break;
            }

            // RFC 9113 §3.2 HTTP/1.1 to HTTP/2 Cleartext Upgrade
            if (req.is_upgrade_h2c()) {
                if (!resp_batch.empty()) {
                    int sent = co_await loop_.ring().send(client_fd_, resp_batch);
                    if (sent != static_cast<int>(resp_batch.size())) [[unlikely]] {
                        if (sent > 0) {
                            (void)(co_await loop_.ring().send_all(client_fd_, std::string_view(resp_batch).substr(sent)));
                        }
                    }
                    resp_batch.clear();
                }
                std::string upgrade_res =
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Connection: Upgrade\r\n"
                    "Upgrade: h2c\r\n\r\n";
                (void)(co_await loop_.ring().send_all(client_fd_, upgrade_res));
                req_offset += bytes_consumed;
                std::string trailing;
                if (req_offset < req_accum.size()) {
                    trailing = req_accum.substr(req_offset);
                }
                std::string h2_settings = std::string(req.headers().get("HTTP2-Settings").value_or(""));
                if (on_h2_upgrade) {
                    co_await on_h2_upgrade(client_fd_, std::move(req), std::move(h2_settings), std::move(trailing));
                }
                co_return;
            }

            // RFC 6455 WebSocket Upgrade
            if (req.is_websocket_upgrade()) {
                if (auto ver_res = validate_websocket_upgrade(req)) {
                    std::string out;
                    Http1Serializer::serialize_response(*ver_res, out);
                    (void)(co_await loop_.ring().send_all(client_fd_, out));
                    keep_alive = false;
                    break;
                }

                const auto* ws_entry = router_.find_ws(req.path());
                if (!ws_entry) {
                    Response not_found;
                    not_found.status(StatusCode::NotFound).text("WebSocket endpoint not found");
                    std::string out;
                    Http1Serializer::serialize_response(not_found, out);
                    (void)(co_await loop_.ring().send_all(client_fd_, out));
                    keep_alive = false;
                    break;
                }
                std::string_view key = req.sec_websocket_key();
                if (key.empty()) {
                    if (auto k = req.headers().get("Sec-WebSocket-Key")) {
                        key = *k;
                    }
                }

                if (key.empty()) {
                    Response bad_res;
                    bad_res.status(StatusCode::BadRequest).text("Missing Sec-WebSocket-Key");
                    std::string out;
                    Http1Serializer::serialize_response(bad_res, out);
                    (void)(co_await loop_.ring().send_all(client_fd_, out));
                    keep_alive = false;
                    break;
                }

                if (!resp_batch.empty()) {
                    int sent = co_await loop_.ring().send(client_fd_, resp_batch);
                    if (sent != static_cast<int>(resp_batch.size())) [[unlikely]] {
                        if (sent > 0) {
                            (void)(co_await loop_.ring().send_all(client_fd_, std::string_view(resp_batch).substr(sent)));
                        }
                    }
                    resp_batch.clear();
                }

                std::string accept_val = websocket::compute_accept_key(key);
                std::string upgrade_res = websocket::build_handshake_response(accept_val);
                (void)(co_await loop_.ring().send_all(client_fd_, upgrade_res));

                std::string trailing;
                if (req_offset + bytes_consumed < req_accum.size()) {
                    trailing = req_accum.substr(req_offset + bytes_consumed);
                }

                websocket::WebSocketConnection ws_conn(loop_, client_fd_, std::string(req.path()),
                                                      ws_entry->handler, ws_entry->echo_handler);
                co_await ws_conn.run(std::move(trailing));
                co_return;
            }

            Response res;
            co_await router_.dispatch(req, res, services_);

            keep_alive = evaluate_keep_alive(req, res);

            if (res.has_file()) {
                if (!resp_batch.empty()) {
                    int sent = co_await loop_.ring().send(client_fd_, resp_batch);
                    if (sent != static_cast<int>(resp_batch.size())) [[unlikely]] {
                        if (sent <= 0) { keep_alive = false; break; }
                        int rem = co_await loop_.ring().send_all(client_fd_, std::string_view(resp_batch).substr(sent));
                        if (rem != static_cast<int>(resp_batch.size() - sent)) { keep_alive = false; break; }
                    }
                    resp_batch.clear();
                }
                std::string header_out;
                Http1Serializer::serialize_headers(res, header_out);
                (void)(co_await loop_.ring().send_all(client_fd_, header_out));
                if (req.method() != Method::HEAD) {
                    co_await stream_file_zero_copy(loop_, client_fd_, res.file_path(), res.file_size());
                }
            } else {
                if (req.method() == Method::HEAD) {
                    Http1Serializer::append_headers(res, resp_batch);
                } else {
                    Http1Serializer::append_response(res, resp_batch);
                }
            }

            req_offset += bytes_consumed;

            if (!keep_alive) {
                break;
            }
        }

        if (req_offset >= req_accum.size()) {
            req_accum.clear();
        } else if (req_offset > 0) {
            req_accum.erase(0, req_offset);
        }

        if (!resp_batch.empty()) {
            int sent = co_await loop_.ring().send(client_fd_, resp_batch);
            if (sent != static_cast<int>(resp_batch.size())) [[unlikely]] {
                if (sent <= 0) break;
                int rem = co_await loop_.ring().send_all(client_fd_, std::string_view(resp_batch).substr(sent));
                if (rem != static_cast<int>(resp_batch.size() - sent)) break;
            }
            resp_batch.clear();
        }

        if (!keep_alive) {
            break;
        }
    }

    (void)(co_await loop_.ring().shutdown(client_fd_, SHUT_WR));
    (void)(co_await loop_.ring().close(client_fd_));
}

core::Task<void> Http1Connection::run_tls(tls::TlsStream& tls_stream, std::string_view alt_svc_hdr) {
    std::string req_accum;
    req_accum.reserve(4096);
    std::string resp_batch;
    resp_batch.reserve(4096);
    char read_buf[8192];

    while (true) {
        int n = co_await tls_stream.read_plaintext(read_buf, sizeof(read_buf));
        if (n <= 0) break;

        req_accum.append(read_buf, static_cast<size_t>(n));
        bool keep_alive = true;
        size_t req_offset = 0;

        while (req_offset < req_accum.size()) {
            std::string_view unparsed(req_accum.data() + req_offset, req_accum.size() - req_offset);
            Request req;
            size_t bytes_consumed = 0;
            auto status = Http1Parser::parse(unparsed, req, bytes_consumed, config_.limits);

            if (status == ParseStatus::NeedMoreData) {
                if (req.expect_continue()) {
                    req.set_expect_continue(false);
                    std::string cont = "HTTP/1.1 100 Continue\r\n\r\n";
                    (void)(co_await tls_stream.write_plaintext(cont.data(), cont.size()));
                }
                break;
            }

            if (auto err_res = make_parse_error_response(status)) {
                std::string out;
                Http1Serializer::serialize_response(*err_res, out);
                (void)(co_await tls_stream.write_plaintext(out.data(), out.size()));
                keep_alive = false;
                break;
            }

            if (req.is_websocket_upgrade()) {
                if (auto ver_res = validate_websocket_upgrade(req)) {
                    std::string out;
                    Http1Serializer::serialize_response(*ver_res, out);
                    (void)(co_await tls_stream.write_plaintext(out.data(), out.size()));
                    keep_alive = false;
                    break;
                }
            }

            Response res;
            co_await router_.dispatch(req, res, services_);

            keep_alive = evaluate_keep_alive(req, res);

            if (!alt_svc_hdr.empty() && !res.headers().contains("alt-svc")) {
                res.set_header_owned("alt-svc", std::string(alt_svc_hdr));
            }

            if (req.method() == Method::HEAD) {
                Http1Serializer::append_headers(res, resp_batch);
            } else {
                Http1Serializer::append_response(res, resp_batch);
            }
            req_offset += bytes_consumed;

            if (!keep_alive) {
                break;
            }
        }

        if (req_offset >= req_accum.size()) {
            req_accum.clear();
        } else if (req_offset > 0) {
            req_accum.erase(0, req_offset);
        }

        if (!resp_batch.empty()) {
            (void)(co_await tls_stream.write_plaintext(resp_batch.data(), resp_batch.size()));
            resp_batch.clear();
        }

        if (!keep_alive) {
            break;
        }
    }

    (void)(co_await loop_.ring().shutdown(client_fd_, SHUT_WR));
    (void)(co_await loop_.ring().close(client_fd_));
}

} // namespace aegon::http::v1
