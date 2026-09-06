#include "http/Server.h"
#include "http/v1/Http1Parser.h"
#include "http/v2/Http2Connection.h"
#include "http/v2/Http2Frame.h"
#include "http/tls/TlsContext.h"
#include "http/tls/TlsStream.h"
#include "http/v3/Http3Server.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>

namespace aegon::http {

Server::Server() = default;
Server::Server(Router router) : router_(std::move(router)) {}
Server::~Server() = default;

Server& Server::enable_tls(const std::string& cert_file, const std::string& key_file) {
    tls_ctx_ = std::make_unique<tls::TlsContext>();
    if (!cert_file.empty() && !key_file.empty()) {
        if (!tls_ctx_->load_cert_and_key(cert_file, key_file)) {
            throw std::runtime_error("Failed to load TLS certificate and key from files: " + cert_file);
        }
    } else {
        if (!tls_ctx_->generate_self_signed("localhost")) {
            throw std::runtime_error("Failed to generate in-memory self-signed TLS certificate");
        }
    }
    tls_enabled_ = true;
    return *this;
}

int Server::create_listen_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("Failed to create TCP socket");
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        throw std::runtime_error("Failed to bind socket to port " + std::to_string(port_));
    }

    if (::listen(fd, 4096) < 0) {
        close(fd);
        throw std::runtime_error("Failed to listen on socket");
    }

    return fd;
}

core::Task<void> Server::handle_http2_connection(core::EventLoop& loop, int client_fd, std::string initial_data) {
    v2::Http2Connection h2(loop, client_fd, router_, user_state_);
    bool ok = co_await h2.init();
    if (!ok) {
        (void)(co_await loop.ring().close(client_fd));
        co_return;
    }

    if (!initial_data.empty()) {
        ok = co_await h2.feed_data(initial_data.data(), initial_data.size());
        if (!ok) {
            (void)(co_await loop.ring().close(client_fd));
            co_return;
        }
    }

    while (running_ && !h2.is_closed()) {
        auto recv_res = co_await loop.ring().recv_multishot(client_fd, loop.buffer_pool().bgid());
        if (recv_res.bytes <= 0) {
            break;
        }

        auto buf_slice = loop.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        ok = co_await h2.feed_data(buf_slice.data(), buf_slice.size());
        loop.buffer_pool().return_buffer(recv_res.bid);

        if (!ok) {
            break;
        }
    }

    (void)(co_await loop.ring().close(client_fd));
}

core::Task<void> Server::handle_http2_upgrade(core::EventLoop& loop, int client_fd, Request req, std::string http2_settings, std::string initial_data) {
    v2::Http2Connection h2(loop, client_fd, router_, user_state_);
    bool ok = co_await h2.init();
    if (!ok) {
        (void)(co_await loop.ring().close(client_fd));
        co_return;
    }

    ok = co_await h2.upgrade_request(std::move(req), http2_settings);
    if (!ok) {
        (void)(co_await loop.ring().close(client_fd));
        co_return;
    }

    if (!initial_data.empty()) {
        ok = co_await h2.feed_data(initial_data.data(), initial_data.size());
        if (!ok) {
            (void)(co_await loop.ring().close(client_fd));
            co_return;
        }
    }

    while (running_ && !h2.is_closed()) {
        auto recv_res = co_await loop.ring().recv_multishot(client_fd, loop.buffer_pool().bgid());
        if (recv_res.bytes <= 0) {
            break;
        }

        auto buf_slice = loop.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        ok = co_await h2.feed_data(buf_slice.data(), buf_slice.size());
        loop.buffer_pool().return_buffer(recv_res.bid);

        if (!ok) {
            break;
        }
    }

    (void)(co_await loop.ring().close(client_fd));
}

core::Task<void> Server::handle_tls_connection(core::EventLoop& loop, int client_fd) {
    tls::TlsStream tls_stream(loop, client_fd, tls_ctx_->native_handle());
    bool ok = co_await tls_stream.handshake();
    if (!ok) {
        (void)(co_await loop.ring().close(client_fd));
        co_return;
    }

    std::string_view alpn = tls_stream.alpn();

    if (alpn == "h2") {
        // HTTP/2 over TLS (ALPN negotiated "h2")
        v2::OutputSender sender = [&](std::span<const uint8_t> data) -> core::Task<int> {
            co_return co_await tls_stream.write_plaintext(data.data(), data.size());
        };

        v2::Http2Connection h2(loop, client_fd, router_, user_state_, std::move(sender));
        ok = co_await h2.init();
        if (!ok) {
            (void)(co_await loop.ring().close(client_fd));
            co_return;
        }

        char read_buf[4096];
        while (running_ && !h2.is_closed()) {
            int n = co_await tls_stream.read_plaintext(read_buf, sizeof(read_buf));
            if (n <= 0) break;

            ok = co_await h2.feed_data(read_buf, static_cast<size_t>(n));
            if (!ok) break;
        }
    } else {
        // HTTP/1.1 over TLS (ALPN "http/1.1" or fallback)
        std::string req_accum;
        req_accum.reserve(4096);
        char read_buf[4096];

        while (running_) {
            int n = co_await tls_stream.read_plaintext(read_buf, sizeof(read_buf));
            if (n <= 0) break;

            req_accum.append(read_buf, static_cast<size_t>(n));

            Request req;
            size_t bytes_consumed = 0;
            auto status = v1::Http1Parser::parse(req_accum, req, bytes_consumed);

            if (status == v1::ParseStatus::NeedMoreData) {
                if (req.expect_continue()) {
                    req.set_expect_continue(false);
                    std::string cont = "HTTP/1.1 100 Continue\r\n\r\n";
                    (void)(co_await tls_stream.write_plaintext(cont.data(), cont.size()));
                }
                continue;
            }

            if (status == v1::ParseStatus::Error) {
                Response bad_res;
                bad_res.status(StatusCode::BadRequest).text("Bad Request");
                std::string out;
                bad_res.serialize_http1(out);
                (void)(co_await tls_stream.write_plaintext(out.data(), out.size()));
                break;
            }

            if (status == v1::ParseStatus::NotImplemented) {
                Response ni_res;
                ni_res.status(StatusCode::NotImplemented).text("Not Implemented");
                std::string out;
                ni_res.serialize_http1(out);
                (void)(co_await tls_stream.write_plaintext(out.data(), out.size()));
                break;
            }

            Response res;
            auto match_res = router_.match(req);

            if (match_res.route_found && match_res.handler) {
                Context ctx(req, res, user_state_);
                co_await (*match_res.handler)(ctx);
            } else if (match_res.method_not_allowed) {
                res.status(StatusCode::MethodNotAllowed).text("Method Not Allowed");
            } else {
                res.status(StatusCode::NotFound).text("Not Found");
            }

            bool keep_alive = true;
            if (auto conn_hdr = req.headers().get("Connection")) {
                if (iequals(*conn_hdr, "close")) {
                    keep_alive = false;
                }
            }
            if (req.version() == HttpVersion::Http1_0 && !req.headers().contains("Connection")) {
                keep_alive = false;
            }

            if (!keep_alive) {
                res.header("Connection", "close");
            }

            if (http3_enabled_) {
                res.set_header_owned("alt-svc", "h3=\":" + std::to_string(port_) + "\"; ma=86400");
            }

            std::string out;
            res.serialize_http1(out);
            (void)(co_await tls_stream.write_plaintext(out.data(), out.size()));

            req_accum.erase(0, bytes_consumed);

            if (!keep_alive) {
                break;
            }
        }
    }

    (void)(co_await loop.ring().close(client_fd));
}

core::Task<void> Server::handle_connection(core::EventLoop& loop, int client_fd) {
    if (tls_enabled_ && tls_ctx_) {
        co_await handle_tls_connection(loop, client_fd);
        co_return;
    }

    std::string req_accum;
    req_accum.reserve(4096);
    bool first_packet = true;

    while (running_) {
        auto recv_res = co_await loop.ring().recv_multishot(client_fd, loop.buffer_pool().bgid());
        if (recv_res.bytes <= 0) {
            break;
        }

        auto buf_slice = loop.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        req_accum.append(reinterpret_cast<const char*>(buf_slice.data()), buf_slice.size());
        loop.buffer_pool().return_buffer(recv_res.bid);

        if (first_packet) {
            first_packet = false;
            if (req_accum.starts_with(v2::CLIENT_PREFACE)) {
                co_await handle_http2_connection(loop, client_fd, std::move(req_accum));
                co_return;
            }
        }

        Request req;
        size_t bytes_consumed = 0;
        auto status = v1::Http1Parser::parse(req_accum, req, bytes_consumed);

        if (status == v1::ParseStatus::NeedMoreData) {
            if (req.expect_continue()) {
                req.set_expect_continue(false);
                (void)(co_await loop.ring().send(client_fd, "HTTP/1.1 100 Continue\r\n\r\n"));
            }
            continue;
        }

        if (status == v1::ParseStatus::Error) {
            Response bad_res;
            bad_res.status(StatusCode::BadRequest).text("Bad Request");
            std::string out;
            bad_res.serialize_http1(out);
            (void)(co_await loop.ring().send(client_fd, out));
            break;
        }

        if (status == v1::ParseStatus::NotImplemented) {
            Response ni_res;
            ni_res.status(StatusCode::NotImplemented).text("Not Implemented");
            std::string out;
            ni_res.serialize_http1(out);
            (void)(co_await loop.ring().send(client_fd, out));
            break;
        }

        // RFC 9113 §3.2 HTTP/1.1 to HTTP/2 Cleartext Upgrade
        if (req.is_upgrade_h2c()) {
            std::string upgrade_res =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Connection: Upgrade\r\n"
                "Upgrade: h2c\r\n\r\n";
            (void)(co_await loop.ring().send(client_fd, upgrade_res));
            req_accum.erase(0, bytes_consumed);
            std::string h2_settings = std::string(req.headers().get("HTTP2-Settings").value_or(""));
            co_await handle_http2_upgrade(loop, client_fd, std::move(req), std::move(h2_settings), std::move(req_accum));
            co_return;
        }

        Response res;
        auto match_res = router_.match(req);

        if (match_res.route_found && match_res.handler) {
            Context ctx(req, res, user_state_);
            co_await (*match_res.handler)(ctx);
        } else if (match_res.method_not_allowed) {
            res.status(StatusCode::MethodNotAllowed).text("Method Not Allowed");
        } else {
            res.status(StatusCode::NotFound).text("Not Found");
        }

        bool keep_alive = true;
        if (auto conn_hdr = req.headers().get("Connection")) {
            if (iequals(*conn_hdr, "close")) {
                keep_alive = false;
            }
        }
        if (req.version() == HttpVersion::Http1_0 && !req.headers().contains("Connection")) {
            keep_alive = false;
        }

        if (!keep_alive) {
            res.header("Connection", "close");
        }

        std::string out;
        res.serialize_http1(out);
        (void)(co_await loop.ring().send(client_fd, out));

        req_accum.erase(0, bytes_consumed);

        if (!keep_alive) {
            break;
        }
    }

    (void)(co_await loop.ring().close(client_fd));
}

core::Task<void> Server::accept_loop(core::EventLoop& loop, int listen_fd) {
    while (running_) {
        auto accept_res = co_await loop.ring().accept(listen_fd);
        if (accept_res.fd < 0) {
            break;
        }
        loop.spawn(handle_connection(loop, accept_res.fd));
    }
}

void Server::run() {
    running_ = true;
    int listen_fd = create_listen_socket();

    core::EventLoop loop(4096, 512, 4096);
    loop.spawn(accept_loop(loop, listen_fd));

    if (tls_enabled_ && http3_enabled_ && tls_ctx_) {
        h3_server_ = std::make_unique<v3::Http3Server>(loop, port_, router_, tls_ctx_->native_handle(), user_state_);
        if (h3_server_->start()) {
            loop.spawn(h3_server_->run_receive_loop());
            loop.spawn(h3_server_->run_timer_loop());
        }
    }

    loop.run();

    if (h3_server_) {
        h3_server_->stop();
    }
    close(listen_fd);
}

void Server::run(size_t threads) {
    running_ = true;
    workers_.clear();

    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this, i]() {
            try {
                int listen_fd = create_listen_socket();
                core::EventLoop loop(4096, 512, 4096);
                loop.pin_to_core(i);
                loop.spawn(accept_loop(loop, listen_fd));

                std::unique_ptr<v3::Http3Server> h3_worker;
                if (tls_enabled_ && http3_enabled_ && tls_ctx_) {
                    h3_worker = std::make_unique<v3::Http3Server>(loop, port_, router_, tls_ctx_->native_handle(), user_state_);
                    if (h3_worker->start()) {
                        loop.spawn(h3_worker->run_receive_loop());
                        loop.spawn(h3_worker->run_timer_loop());
                    }
                }

                loop.run();

                if (h3_worker) {
                    h3_worker->stop();
                }
                close(listen_fd);
            } catch (const std::exception& e) {
                std::cerr << "Worker thread " << i << " error: " << e.what() << "\n";
            }
        });
    }

    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
}

void Server::stop() {
    running_ = false;
    if (h3_server_) {
        h3_server_->stop();
    }
}

} // namespace aegon::http
