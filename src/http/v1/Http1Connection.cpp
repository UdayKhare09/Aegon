#include "http/v1/Http1Connection.h"
#include "http/v2/Http2Frame.h"
#include "http/websocket/WebSocketHandshake.h"
#include "http/websocket/WebSocketConnection.h"
#include "core/OsSocket.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <algorithm>

namespace aegon::http::v1 {

Http1Connection::Http1Connection(core::EventLoop& loop, core::os_socket_t client_fd, const Router& router,
                                 const ServiceRegistry* services, const ServerConfig& config)
    : loop_(&loop), client_fd_(client_fd), router_(&router), services_(services), config_(config),
      ctx_(req_, res_, services_) {
    req_accum_.reserve(16384);
    resp_batch_.reserve(16384);
}

void Http1Connection::reset(core::os_socket_t client_fd) noexcept {
    client_fd_ = client_fd;
    req_accum_.clear();
    if (req_accum_.capacity() < 16384) req_accum_.reserve(16384);
    resp_batch_.clear();
    if (resp_batch_.capacity() < 16384) resp_batch_.reserve(16384);
    req_.reset();
    res_.reset();
    ctx_.reset();
}

void Http1Connection::reset(core::EventLoop& loop, core::os_socket_t client_fd, const Router& router,
                            const ServiceRegistry* services, const ServerConfig& config) noexcept {
    loop_ = &loop;
    client_fd_ = client_fd;
    router_ = &router;
    services_ = services;
    config_ = config;
    req_accum_.clear();
    if (req_accum_.capacity() < 16384) req_accum_.reserve(16384);
    resp_batch_.clear();
    if (resp_batch_.capacity() < 16384) resp_batch_.reserve(16384);
    req_.reset();
    res_.reset();
    ctx_.reset();
    ctx_.set_services(services);
}

void Http1Connection::cleanup() noexcept {
    client_fd_ = core::INVALID_OS_SOCKET;
    loop_ = nullptr;
    router_ = nullptr;
    services_ = nullptr;
    if (req_accum_.capacity() > 65536) {
        req_accum_.shrink_to_fit();
        req_accum_.reserve(16384);
    } else {
        req_accum_.clear();
    }
    if (resp_batch_.capacity() > 65536) {
        resp_batch_.shrink_to_fit();
        resp_batch_.reserve(16384);
    } else {
        resp_batch_.clear();
    }
    req_.reset();
    res_.reset();
    ctx_.reset();
}

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

core::Task<bool> Http1Connection::stream_file(core::EventLoop& loop, core::os_socket_t client_fd,
                                              const std::string& file_path, size_t file_size,
                                              std::shared_ptr<storage::File> file_handle) {
    if (file_size == 0) co_return true;

    int file_fd = -1;
    bool should_close_fd = false;
    if (file_handle && file_handle->is_valid()) {
        file_fd = file_handle->fd();
    } else if (!file_path.empty()) {
        file_fd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (file_fd >= 0) should_close_fd = true;
    }

    if (file_fd >= 0) {
        off_t offset = 0;
        size_t remaining = file_size;
        bool ok = true;
        while (remaining > 0) {
            size_t to_send = std::min<size_t>(remaining, 1024 * 1024);
            ssize_t sent = ::sendfile(client_fd, file_fd, &offset, to_send);
            if (sent > 0) {
                remaining -= static_cast<size_t>(sent);
            } else if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    (void)co_await loop.engine().poll(client_fd, POLLOUT);
                } else if (errno == EINTR) {
                    continue;
                } else {
                    ok = false;
                    break;
                }
            } else {
                break;
            }
        }
        if (should_close_fd) {
            ::close(file_fd);
        }
        if (ok && remaining == 0) {
            co_return true;
        }
    }

    if (file_handle) {
        try {
            storage::FileReadStream stream(*file_handle, 64 * 1024, 0, static_cast<int64_t>(file_size));
            while (auto chunk = co_await stream.next_chunk()) {
                int sent = co_await loop.engine().send_all(client_fd,
                    std::string_view(reinterpret_cast<const char*>(chunk->data()), chunk->size()));
                if (sent <= 0) co_return false;
            }
            co_return true;
        } catch (...) {
            co_return false;
        }
    }

    int fd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) co_return false;
    char buf[65536];
    size_t remaining = file_size;
    off_t offset = 0;
    bool ok = true;
    while (remaining > 0) {
        size_t to_read = std::min(remaining, sizeof(buf));
        ssize_t n = ::pread(fd, buf, to_read, offset);
        if (n <= 0) { ok = false; break; }
        int sent = co_await loop.engine().send_all(client_fd, std::string_view(buf, static_cast<size_t>(n)));
        if (sent <= 0) { ok = false; break; }
        offset += n;
        remaining -= static_cast<size_t>(n);
    }
    ::close(fd);
    co_return ok;
}

core::Task<bool> Http1Connection::stream_file_zero_copy(core::EventLoop& loop, core::os_socket_t client_fd,
                                                        const std::string& file_path, size_t file_size) {
    return stream_file(loop, client_fd, file_path, file_size, nullptr);
}

core::Task<bool> Http1Connection::flush_batch() {
    if (resp_batch_.empty()) co_return true;
    // Common case: the socket takes the whole batch now, with no ring round trip and no
    // suspension, so the connection goes straight back to waiting on its armed recv.
    int sent = core::IoEngine::send_now(client_fd_, resp_batch_.data(), resp_batch_.size());
    if (sent == static_cast<int>(resp_batch_.size())) [[likely]] {
        resp_batch_.clear();
        co_return true;
    }
    if (sent < 0 && sent != -EAGAIN && sent != -EWOULDBLOCK && sent != -EINTR) {
        resp_batch_.clear();
        co_return false;
    }
    size_t done = sent > 0 ? static_cast<size_t>(sent) : 0;
    std::string_view rest = std::string_view(resp_batch_).substr(done);
    int rem = co_await loop_->engine().send_all(client_fd_, rest);
    bool ok = rem == static_cast<int>(rest.size());
    resp_batch_.clear();
    co_return ok;
}

core::Task<void> Http1Connection::run(H2DirectCallback on_h2_direct,
                                      H2UpgradeCallback on_h2_upgrade) {
    bool first_packet = true;
    // Armed once; the kernel keeps receiving into provided buffers between requests.
    auto stream = loop_->engine().recv_multishot_stream(client_fd_, loop_->buffer_pool().bgid());
    bool keep_alive = true;

    while (keep_alive) {
        auto recv_res = co_await stream.next();
        if (recv_res.bytes <= 0 || !recv_res.buffer_valid) {
            break;
        }

        auto buf_slice = loop_->buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        const std::string_view slice(reinterpret_cast<const char*>(buf_slice.data()), buf_slice.size());

        // Parse straight out of the provided buffer unless a partial request is carried over.
        // Anything that suspends (an async handler, an upgrade, a file) first moves the rest
        // of the bytes into req_accum_ (detach_from_buffer), so no provided buffer is ever
        // held across a suspension.
        bool in_buffer = req_accum_.empty();
        bool buffer_held = true;
        if (!in_buffer) {
            req_accum_.append(slice);
            loop_->buffer_pool().return_buffer(recv_res.bid);
            buffer_held = false;
        }
        std::string_view data = in_buffer ? slice : std::string_view(req_accum_);
        size_t off = 0;

        auto release_buffer = [&]() {
            if (buffer_held) {
                loop_->buffer_pool().return_buffer(recv_res.bid);
                buffer_held = false;
            }
        };
        // Moves data[off..] into req_accum_ and re-parses the current request from there,
        // so req_ stops pointing into the provided buffer. Its length does not change.
        auto detach_from_buffer = [&]() {
            if (!in_buffer) return;
            req_accum_.assign(data.substr(off));
            release_buffer();
            in_buffer = false;
            data = req_accum_;
            off = 0;
            req_.reset();
            size_t again = 0;
            (void)Http1Parser::parse(data, req_, again, config_.limits);
        };

        while (off < data.size()) {
            std::string_view unparsed = data.substr(off);
            if (first_packet) {
                first_packet = false;
                if (unparsed.starts_with(v2::CLIENT_PREFACE)) {
                    std::string preface(unparsed);
                    release_buffer();
                    req_accum_.clear();
                    (void)co_await flush_batch();
                    if (on_h2_direct) {
                        co_await on_h2_direct(client_fd_, std::move(preface), std::move(stream));
                    }
                    co_return;
                }
            }

            req_.reset();
            size_t bytes_consumed = 0;
            auto status = Http1Parser::parse(unparsed, req_, bytes_consumed, config_.limits);

            if (status == ParseStatus::NeedMoreData) {
                if (req_.expect_continue()) {
                    req_.set_expect_continue(false);
                    resp_batch_.append("HTTP/1.1 100 Continue\r\n\r\n");
                }
                break;
            }

            if (auto err_res = make_parse_error_response(status)) {
                Http1Serializer::append_response(*err_res, resp_batch_);
                keep_alive = false;
                break;
            }

            // RFC 9113 §3.2 HTTP/1.1 to HTTP/2 Cleartext Upgrade
            if (req_.is_upgrade_h2c()) {
                // req_ is handed to the HTTP/2 side: it must view req_accum_ (a member that
                // outlives the upgrade), not the provided buffer.
                detach_from_buffer();
                resp_batch_.append("HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: h2c\r\n\r\n");
                if (!co_await flush_batch()) break;
                std::string trailing(data.substr(off + bytes_consumed));
                std::string h2_settings = std::string(req_.headers().get("HTTP2-Settings").value_or(""));
                if (on_h2_upgrade) {
                    co_await on_h2_upgrade(client_fd_, std::move(req_), std::move(h2_settings), std::move(trailing),
                                           std::move(stream));
                }
                co_return;
            }

            // RFC 6455 WebSocket Upgrade
            if (req_.is_websocket_upgrade()) {
                std::optional<Response> refuse;
                const Router::WebSocketRouteEntry* ws_entry = nullptr;
                std::string_view key;
                if (auto ver_res = validate_websocket_upgrade(req_)) {
                    refuse = std::move(ver_res);
                } else if (!(ws_entry = router_->find_ws(req_.path()))) {
                    Response not_found;
                    not_found.status(StatusCode::NotFound).text("WebSocket endpoint not found");
                    refuse = std::move(not_found);
                } else {
                    key = req_.sec_websocket_key();
                    if (key.empty()) {
                        if (auto k = req_.headers().get("Sec-WebSocket-Key")) key = *k;
                    }
                    if (key.empty()) {
                        Response bad_res;
                        bad_res.status(StatusCode::BadRequest).text("Missing Sec-WebSocket-Key");
                        refuse = std::move(bad_res);
                    }
                }
                if (refuse) {
                    Http1Serializer::append_response(*refuse, resp_batch_);
                    keep_alive = false;
                    break;
                }

                std::string accept_val = websocket::compute_accept_key(key);
                resp_batch_.append(websocket::build_handshake_response(accept_val));
                std::string trailing(data.substr(off + bytes_consumed));
                std::string path(req_.path());
                release_buffer();
                req_accum_.clear();
                if (!co_await flush_batch()) break;

                websocket::WebSocketConnection ws_conn(*loop_, client_fd_, std::move(path),
                                                      ws_entry->handler, ws_entry->echo_handler,
                                                      config_.websocket);
                co_await ws_conn.run(std::move(stream), std::move(trailing));
                co_return;
            }

            res_.reset();
            ctx_.reset();
            bool sse_headers_sent = false;

            if (router_->dispatch_sync(req_, res_, services_, ctx_)) [[likely]] {
                // Executed synchronously inline with 0 coroutines
            } else {
                // The handler may suspend: stop referencing the provided buffer first.
                detach_from_buffer();
                struct SseContext {
                    Http1Connection& conn;
                    Response& res;
                    bool& sse_headers_sent;
                } sse_ctx{*this, res_, sse_headers_sent};

                SseProvider sse_provider{
                    .user_data = &sse_ctx,
                    .build = [](void* ud) -> SseWriteFn {
                        auto* c = static_cast<SseContext*>(ud);
                        return [c](std::string frame) -> core::Task<void> {
                            if (!c->sse_headers_sent) {
                                Http1Serializer::append_headers(c->res, c->conn.resp_batch_);
                                c->sse_headers_sent = true;
                            }
                            Http1Serializer::serialize_chunk(frame, c->conn.resp_batch_);
                            (void)co_await c->conn.flush_batch();
                        };
                    }
                };

                co_await router_->dispatch(req_, res_, services_, nullptr, sse_provider);
            }

            keep_alive = evaluate_keep_alive(req_, res_);

            if (res_.has_file()) {
                // Streaming the file suspends: stop referencing the provided buffer first.
                detach_from_buffer();
                Http1Serializer::append_headers(res_, resp_batch_);
                if (!co_await flush_batch()) { keep_alive = false; break; }
                if (req_.method() != Method::HEAD && res_.file_size() > 0) {
                    bool ok = co_await stream_file(*loop_, client_fd_, res_.file_path(), res_.file_size(), res_.file_handle());
                    if (!ok) {
                        keep_alive = false;
                        break;
                    }
                }
            } else if (res_.is_sse()) {
                if (!sse_headers_sent) {
                    Http1Serializer::append_headers(res_, resp_batch_);
                    sse_headers_sent = true;
                }
                resp_batch_.append("0\r\n\r\n");
                keep_alive = false;  // SSE connections close after the stream ends
            } else if (req_.method() == Method::HEAD) {
                Http1Serializer::append_headers(res_, resp_batch_);
            } else {
                Http1Serializer::append_response(res_, resp_batch_);
            }

            off += bytes_consumed;
            if (!keep_alive) {
                break;
            }
        }

        // Keep only an unparsed partial request; the provided buffer goes back to the ring.
        if (in_buffer) {
            if (off < data.size() && keep_alive) {
                req_accum_.assign(data.substr(off));
            }
            release_buffer();
        } else if (off >= req_accum_.size()) {
            req_accum_.clear();
        } else if (off > 0) {
            req_accum_.erase(0, off);
        }

        if (!co_await flush_batch()) {
            break;
        }
    }

    stream.cancel();
    (void)(co_await loop_->engine().close(client_fd_));
}

core::Task<void> Http1Connection::run_tls(net::tls::TlsStream& tls_stream, std::string_view alt_svc_hdr) {
    constexpr size_t kReadChunk = 16384;  // one TLS record at most

    while (true) {
        // Decrypt straight into the request buffer's tail (no bounce through a stack buffer).
        // resize_and_overwrite leaves the new bytes uninitialised; read_plaintext fills them.
        const size_t old_size = req_accum_.size();
        req_accum_.resize_and_overwrite(old_size + kReadChunk, [](char*, size_t n) { return n; });
        int n = co_await tls_stream.read_plaintext(req_accum_.data() + old_size, kReadChunk);
        req_accum_.resize(old_size + (n > 0 ? static_cast<size_t>(n) : 0));
        if (n <= 0) break;
        bool keep_alive = true;
        size_t req_offset = 0;

        while (req_offset < req_accum_.size()) {
            std::string_view unparsed(req_accum_.data() + req_offset, req_accum_.size() - req_offset);
            req_.reset();
            size_t bytes_consumed = 0;
            auto status = Http1Parser::parse(unparsed, req_, bytes_consumed, config_.limits);

            if (status == ParseStatus::NeedMoreData) {
                if (req_.expect_continue()) {
                    req_.set_expect_continue(false);
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

            if (req_.is_websocket_upgrade()) {
                if (auto ver_res = validate_websocket_upgrade(req_)) {
                    std::string out;
                    Http1Serializer::serialize_response(*ver_res, out);
                    (void)(co_await tls_stream.write_plaintext(out.data(), out.size()));
                    keep_alive = false;
                    break;
                }
            }

            res_.reset();
            ctx_.reset();
            bool sse_headers_sent = false;

            if (router_->dispatch_sync(req_, res_, services_, ctx_)) [[likely]] {
                // Executed synchronously inline with 0 coroutines
            } else {
                struct SseTlsContext {
                    net::tls::TlsStream& tls_stream;
                    std::string_view alt_svc_hdr;
                    Response& res;
                    std::string& resp_batch;
                    bool& sse_headers_sent;
                } sse_tls_ctx{tls_stream, alt_svc_hdr, res_, resp_batch_, sse_headers_sent};

                SseProvider sse_provider{
                    .user_data = &sse_tls_ctx,
                    .build = [](void* ud) -> SseWriteFn {
                        auto* c = static_cast<SseTlsContext*>(ud);
                        return [c](std::string frame) -> core::Task<void> {
                            if (!c->sse_headers_sent) {
                                if (!c->resp_batch.empty()) {
                                    (void)(co_await c->tls_stream.write_plaintext(c->resp_batch.data(), c->resp_batch.size()));
                                    c->resp_batch.clear();
                                }
                                if (!c->alt_svc_hdr.empty() && !c->res.headers().contains("alt-svc")) {
                                    c->res.set_header_owned("alt-svc", std::string(c->alt_svc_hdr));
                                }
                                std::string header_out;
                                Http1Serializer::serialize_headers(c->res, header_out);
                                (void)(co_await c->tls_stream.write_plaintext(header_out.data(), header_out.size()));
                                c->sse_headers_sent = true;
                            }
                            std::string chunk;
                            chunk.reserve(frame.size() + 16);
                            Http1Serializer::serialize_chunk(frame, chunk);
                            (void)(co_await c->tls_stream.write_plaintext(chunk.data(), chunk.size()));
                        };
                    }
                };

                co_await router_->dispatch(req_, res_, services_, nullptr, sse_provider);
            }

            keep_alive = evaluate_keep_alive(req_, res_);

            if (!alt_svc_hdr.empty() && !res_.headers().contains("alt-svc")) {
                res_.set_header_owned("alt-svc", std::string(alt_svc_hdr));
            }

            if (res_.has_file()) {
                if (!resp_batch_.empty()) {
                    (void)(co_await tls_stream.write_plaintext(resp_batch_.data(), resp_batch_.size()));
                    resp_batch_.clear();
                }
                std::string header_out;
                Http1Serializer::serialize_headers(res_, header_out);
                (void)(co_await tls_stream.write_plaintext(header_out.data(), header_out.size()));
                if (req_.method() != Method::HEAD && res_.file_size() > 0) {
                    bool served = false;
                    int file_fd = -1;
                    bool should_close_fd = false;
                    if (res_.file_handle() && res_.file_handle()->is_valid()) {
                        file_fd = res_.file_handle()->fd();
                    } else if (!res_.file_path().empty()) {
                        file_fd = ::open(res_.file_path().c_str(), O_RDONLY | O_CLOEXEC);
                        if (file_fd >= 0) should_close_fd = true;
                    }

                    if (file_fd >= 0) {
                        void* mapped = ::mmap(nullptr, res_.file_size(), PROT_READ, MAP_SHARED, file_fd, 0);
                        if (should_close_fd) ::close(file_fd);
                        if (mapped != MAP_FAILED) {
                            ::madvise(mapped, res_.file_size(), MADV_SEQUENTIAL | MADV_WILLNEED);
                            const char* p = static_cast<const char*>(mapped);
                            size_t remaining = res_.file_size();
                            size_t off = 0;
                            while (remaining > 0) {
                                size_t chunk_len = std::min<size_t>(remaining, 65536);
                                int written = co_await tls_stream.write_plaintext(p + off, chunk_len);
                                if (written <= 0) {
                                    keep_alive = false;
                                    break;
                                }
                                off += static_cast<size_t>(written);
                                remaining -= static_cast<size_t>(written);
                            }
                            ::munmap(mapped, res_.file_size());
                            served = true;
                        }
                    }
                    if (!served) {
                        if (!res_.file_handle()) {
                            int file_fd = ::open(res_.file_path().c_str(), O_RDONLY | O_CLOEXEC);
                            if (file_fd < 0) {
                                keep_alive = false;
                            } else {
                                char buf[65536];
                                size_t remaining = res_.file_size();
                                off_t offset = 0;
                                while (remaining > 0) {
                                    size_t to_read = std::min(remaining, sizeof(buf));
                                    ssize_t n = ::pread(file_fd, buf, to_read, offset);
                                    if (n <= 0) { keep_alive = false; break; }
                                    int written = co_await tls_stream.write_plaintext(buf, static_cast<size_t>(n));
                                    if (written <= 0) { keep_alive = false; break; }
                                    offset += n;
                                    remaining -= static_cast<size_t>(n);
                                }
                                ::close(file_fd);
                            }
                        } else {
                            try {
                                storage::FileReadStream stream(*res_.file_handle(), 64 * 1024, 0, static_cast<int64_t>(res_.file_size()));
                                while (auto chunk = co_await stream.next_chunk()) {
                                    int written = co_await tls_stream.write_plaintext(
                                        reinterpret_cast<const char*>(chunk->data()), chunk->size());
                                    if (written <= 0) {
                                        keep_alive = false;
                                        break;
                                    }
                                }
                            } catch (...) {
                                keep_alive = false;
                            }
                        }
                    }
                }
            } else if (res_.is_sse()) {
                if (!sse_headers_sent) {
                    if (!resp_batch_.empty()) {
                        (void)(co_await tls_stream.write_plaintext(resp_batch_.data(), resp_batch_.size()));
                        resp_batch_.clear();
                    }
                    std::string header_out;
                    Http1Serializer::serialize_headers(res_, header_out);
                    (void)(co_await tls_stream.write_plaintext(header_out.data(), header_out.size()));
                    sse_headers_sent = true;
                }
                (void)(co_await tls_stream.write_plaintext("0\r\n\r\n", 5));
                keep_alive = false;
            } else if (req_.method() == Method::HEAD) {
                Http1Serializer::append_headers(res_, resp_batch_);
            } else {
                Http1Serializer::append_response(res_, resp_batch_);
            }
            req_offset += bytes_consumed;

            if (!keep_alive) {
                break;
            }
        }

        if (req_offset >= req_accum_.size()) {
            req_accum_.clear();
        } else if (req_offset > 0) {
            req_accum_.erase(0, req_offset);
        }

        if (!resp_batch_.empty()) {
            (void)(co_await tls_stream.write_plaintext(resp_batch_.data(), resp_batch_.size()));
            resp_batch_.clear();
        }

        if (!keep_alive) {
            break;
        }
    }

    (void)(co_await loop_->engine().close(client_fd_));
}

} // namespace aegon::http::v1
