#include "http/Server.h"
#include "http/v1/Http1Parser.h"
#include "http/v2/Http2Connection.h"
#include "http/v2/Http2Frame.h"
#include "http/tls/TlsContext.h"
#include "http/tls/TlsStream.h"
#include "http/v3/Http3Server.h"
#include "http/websocket/WebSocketHandshake.h"
#include "http/websocket/WebSocketConnection.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cerrno>

namespace aegon::http {

Server::Server() = default;
Server::Server(Router router) : router_(std::move(router)) {}
Server::~Server() = default;

Server::Server(Server&& other) noexcept
    : router_(std::move(other.router_)),
      host_(std::move(other.host_)),
      port_(other.port_),
      listeners_(std::move(other.listeners_)),
      services_(std::move(other.services_)),
      startup_hooks_(std::move(other.startup_hooks_)),
      shutdown_hooks_(std::move(other.shutdown_hooks_)),
      background_workers_(std::move(other.background_workers_)),
      running_(other.running_.load()),
      workers_(std::move(other.workers_)),
      sqpoll_enabled_(other.sqpoll_enabled_),
      sq_thread_idle_ms_(other.sq_thread_idle_ms_),
      sq_thread_cpu_(other.sq_thread_cpu_),
      ring_entries_(other.ring_entries_),
      buffer_pool_entries_(other.buffer_pool_entries_),
      tls_enabled_(other.tls_enabled_),
      http3_enabled_(other.http3_enabled_),
      tls_ctx_(std::move(other.tls_ctx_)),
      h3_servers_(std::move(other.h3_servers_))
{
}

Server& Server::operator=(Server&& other) noexcept {
    if (this != &other) {
        router_ = std::move(other.router_);
        host_ = std::move(other.host_);
        port_ = other.port_;
        listeners_ = std::move(other.listeners_);
        services_ = std::move(other.services_);
        startup_hooks_ = std::move(other.startup_hooks_);
        shutdown_hooks_ = std::move(other.shutdown_hooks_);
        background_workers_ = std::move(other.background_workers_);
        running_.store(other.running_.load());
        workers_ = std::move(other.workers_);
        sqpoll_enabled_ = other.sqpoll_enabled_;
        sq_thread_idle_ms_ = other.sq_thread_idle_ms_;
        sq_thread_cpu_ = other.sq_thread_cpu_;
        ring_entries_ = other.ring_entries_;
        buffer_pool_entries_ = other.buffer_pool_entries_;
        tls_enabled_ = other.tls_enabled_;
        http3_enabled_ = other.http3_enabled_;
        tls_ctx_ = std::move(other.tls_ctx_);
        h3_servers_ = std::move(other.h3_servers_);
    }
    return *this;
}

Server& Server::enable_tls(const std::string& cert_file, const std::string& key_file) {
    tls_ctx_ = std::make_unique<tls::TlsContext>();
    if (!cert_file.empty() && !key_file.empty()) {
        if (::access(cert_file.c_str(), R_OK) != 0) {
            throw std::runtime_error("Server TLS configuration error: certificate file does not exist or is unreadable: " + cert_file);
        }
        if (::access(key_file.c_str(), R_OK) != 0) {
            throw std::runtime_error("Server TLS configuration error: key file does not exist or is unreadable: " + key_file);
        }
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

int Server::create_listen_socket(uint16_t port, const std::string& host) {
    if (port == 0) {
        throw std::runtime_error("Server configuration error: port must be greater than 0 (1-65535)");
    }

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
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        throw std::runtime_error("Failed to bind socket to port " + std::to_string(port));
    }

    if (::listen(fd, 4096) < 0) {
        close(fd);
        throw std::runtime_error("Failed to listen on socket");
    }

    return fd;
}

core::Task<void> Server::handle_http2_connection(core::EventLoop& loop, int client_fd, std::string initial_data) {
    v2::Http2Connection h2(loop, client_fd, router_, services_.get());
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
    v2::Http2Connection h2(loop, client_fd, router_, services_.get());
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

core::Task<void> Server::handle_tls_connection(core::EventLoop& loop, int client_fd, uint16_t port) {
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

        v2::Http2Connection h2(loop, client_fd, router_, services_.get(), std::move(sender));
        if (http3_enabled_) {
            h2.set_alt_svc("h3=\":" + std::to_string(port) + "\"; ma=86400");
        }
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
        std::string resp_batch;
        resp_batch.reserve(4096);
        char read_buf[8192];

        // Progressive protocol ladder on HTTP/1.1 TLS
        const std::string alt_svc_hdr = http3_enabled_
            ? ("h3=\":" + std::to_string(port) + "\"; ma=86400, h2=\":" + std::to_string(port) + "\"; ma=86400")
            : ("h2=\":" + std::to_string(port) + "\"; ma=86400");

        while (running_) {
            int n = co_await tls_stream.read_plaintext(read_buf, sizeof(read_buf));
            if (n <= 0) break;

            req_accum.append(read_buf, static_cast<size_t>(n));
            bool keep_alive = true;

            while (!req_accum.empty()) {
                Request req;
                size_t bytes_consumed = 0;
                auto status = v1::Http1Parser::parse(req_accum, req, bytes_consumed);

                if (status == v1::ParseStatus::NeedMoreData) {
                    if (req.expect_continue()) {
                        req.set_expect_continue(false);
                        std::string cont = "HTTP/1.1 100 Continue\r\n\r\n";
                        (void)(co_await tls_stream.write_plaintext(cont.data(), cont.size()));
                    }
                    break;
                }

                if (status == v1::ParseStatus::Error) {
                    Response bad_res;
                    bad_res.status(StatusCode::BadRequest).text("Bad Request");
                    std::string out;
                    bad_res.serialize_http1(out);
                    (void)(co_await tls_stream.write_plaintext(out.data(), out.size()));
                    keep_alive = false;
                    break;
                }

                if (status == v1::ParseStatus::NotImplemented) {
                    Response ni_res;
                    ni_res.status(StatusCode::NotImplemented).text("Not Implemented");
                    std::string out;
                    ni_res.serialize_http1(out);
                    (void)(co_await tls_stream.write_plaintext(out.data(), out.size()));
                    keep_alive = false;
                    break;
                }

                Response res;
                co_await router_.dispatch(req, res, services_.get());

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

                if (!alt_svc_hdr.empty()) {
                    res.set_header_owned("alt-svc", alt_svc_hdr);
                }

                res.append_http1(resp_batch);
                req_accum.erase(0, bytes_consumed);

                if (!keep_alive) {
                    break;
                }
            }

            if (!resp_batch.empty()) {
                (void)(co_await tls_stream.write_plaintext(resp_batch.data(), resp_batch.size()));
                resp_batch.clear();
            }

            if (!keep_alive) {
                break;
            }
        }
    }

    (void)(co_await loop.ring().close(client_fd));
}

core::Task<void> Server::handle_connection(core::EventLoop& loop, int client_fd) {
    std::string req_accum;
    req_accum.reserve(512);
    std::string resp_batch;
    resp_batch.reserve(512);
    bool first_packet = true;

    while (running_) {
        auto recv_res = co_await loop.ring().recv_multishot(client_fd, loop.buffer_pool().bgid());
        if (recv_res.bytes == -ENOBUFS) {
            co_await loop.ring().timeout(100'000ULL);
            continue;
        }
        if (recv_res.bytes <= 0) {
            break;
        }

        auto buf_slice = loop.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        req_accum.append(reinterpret_cast<const char*>(buf_slice.data()), buf_slice.size());
        loop.buffer_pool().return_buffer(recv_res.bid);

        bool keep_alive = true;

        while (!req_accum.empty()) {
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
                break;
            }

            if (status == v1::ParseStatus::Error) {
                Response bad_res;
                bad_res.status(StatusCode::BadRequest).text("Bad Request");
                std::string out;
                bad_res.serialize_http1(out);
                (void)(co_await loop.ring().send(client_fd, out));
                keep_alive = false;
                break;
            }

            if (status == v1::ParseStatus::NotImplemented) {
                Response ni_res;
                ni_res.status(StatusCode::NotImplemented).text("Not Implemented");
                std::string out;
                ni_res.serialize_http1(out);
                (void)(co_await loop.ring().send(client_fd, out));
                keep_alive = false;
                break;
            }

            // RFC 9113 §3.2 HTTP/1.1 to HTTP/2 Cleartext Upgrade
            if (req.is_upgrade_h2c()) {
                if (!resp_batch.empty()) {
                    (void)(co_await loop.ring().send(client_fd, resp_batch));
                    resp_batch.clear();
                }
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

            // RFC 6455 WebSocket Upgrade
            const auto* ws_entry = router_.find_ws(req.path());
            if (ws_entry && req.is_websocket_upgrade()) {
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
                    bad_res.serialize_http1(out);
                    (void)(co_await loop.ring().send(client_fd, out));
                    keep_alive = false;
                    break;
                }

                if (!resp_batch.empty()) {
                    (void)(co_await loop.ring().send(client_fd, resp_batch));
                    resp_batch.clear();
                }

                std::string accept_val = websocket::compute_accept_key(key);
                std::string upgrade_res = websocket::build_handshake_response(accept_val);
                (void)(co_await loop.ring().send(client_fd, upgrade_res));

                std::string trailing;
                if (bytes_consumed < req_accum.size()) {
                    trailing = req_accum.substr(bytes_consumed);
                }

                websocket::WebSocketConnection ws_conn(loop, client_fd, std::string(req.path()),
                                                      ws_entry->handler, ws_entry->echo_handler);
                co_await ws_conn.run(std::move(trailing));
                co_return;
            }

            Response res;
            co_await router_.dispatch(req, res, services_.get());

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

            if (res.has_file()) {
                if (!resp_batch.empty()) {
                    (void)(co_await loop.ring().send(client_fd, resp_batch));
                    resp_batch.clear();
                }
                std::string header_out;
                res.serialize_http1_headers(header_out);
                (void)(co_await loop.ring().send(client_fd, header_out));
                co_await stream_file_zero_copy(loop, client_fd, res.file_path(), res.file_size());
            } else {
                res.append_http1(resp_batch);
            }

            if (bytes_consumed >= req_accum.size()) {
                req_accum.clear();
            } else {
                req_accum.erase(0, bytes_consumed);
            }

            if (!keep_alive) {
                break;
            }
        }

        if (!resp_batch.empty()) {
            (void)(co_await loop.ring().send(client_fd, resp_batch));
            resp_batch.clear();
        }

        if (!keep_alive) {
            break;
        }
    }

    (void)(co_await loop.ring().close(client_fd));
}

core::Task<bool> Server::stream_file_zero_copy(core::EventLoop& loop, int client_fd, const std::string& file_path, size_t file_size) {
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

core::Task<void> Server::accept_loop(core::EventLoop& loop, int listen_fd, uint16_t port, bool is_tls) {
    while (running_) {
        auto stream = loop.ring().accept_multishot(listen_fd);
        while (running_) {
            auto accept_res = co_await stream.next();
            if (accept_res.fd < 0) {
                // Multishot accept stream disarmed (e.g. transient ECONNABORTED).
                // Break inner loop to re-arm multishot accept stream.
                break;
            }
            int nodelay = 1;
            ::setsockopt(accept_res.fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
            if (is_tls && tls_ctx_) {
                loop.spawn(handle_tls_connection(loop, accept_res.fd, port));
            } else {
                loop.spawn(handle_connection(loop, accept_res.fd));
            }
        }
    }
}

void Server::run() {
    running_ = true;

    // 1. Freeze service registry for zero-lock hot-path lookups during request handling
    services_->freeze();

    // 2. Run asynchronous startup lifecycle hooks before listening
    for (const auto& hook : startup_hooks_) {
        core::EventLoop init_loop;
        init_loop.spawn(hook(*this));
        init_loop.run();
    }

    std::vector<ListenerConfig> active_listeners = listeners_;
    if (active_listeners.empty()) {
        active_listeners.push_back(ListenerConfig{port_, host_, tls_enabled_});
    } else if (active_listeners.size() == 1 && tls_enabled_ && !active_listeners[0].tls) {
        active_listeners[0].tls = true;
    }

    struct ActiveListener {
        int fd;
        uint16_t port;
        bool is_tls;
    };
    std::vector<ActiveListener> thread_listeners;
    for (const auto& l : active_listeners) {
        int fd = create_listen_socket(l.port, l.host);
        thread_listeners.push_back({fd, l.port, l.tls});
    }

    core::IoUringConfig ring_cfg;
    ring_cfg.entries = ring_entries_;
    ring_cfg.enable_sqpoll = sqpoll_enabled_;
    ring_cfg.sq_thread_idle_ms = sq_thread_idle_ms_;
    ring_cfg.sq_thread_cpu = sq_thread_cpu_;

    core::EventLoop loop(ring_cfg, buffer_pool_entries_, 4096);
    for (const auto& al : thread_listeners) {
        loop.spawn(accept_loop(loop, al.fd, al.port, al.is_tls));
    }

    // Spawn long-running background workers on the server event loop
    for (const auto& worker : background_workers_) {
        loop.spawn(worker(*this, loop));
    }

    if (tls_enabled_ && http3_enabled_ && tls_ctx_) {
        for (const auto& l : active_listeners) {
            if (l.tls) {
                auto h3 = std::make_unique<v3::Http3Server>(loop, l.port, router_, tls_ctx_->native_handle(), services_.get());
                if (h3->start()) {
                    loop.spawn(h3->run_receive_loop());
                    loop.spawn(h3->run_timer_loop());
                    h3_servers_.push_back(std::move(h3));
                }
            }
        }
    }

    loop.run();

    for (auto& h3 : h3_servers_) {
        if (h3) h3->stop();
    }
    h3_servers_.clear();
    for (const auto& al : thread_listeners) {
        close(al.fd);
    }
}

void Server::run(size_t threads) {
    running_ = true;

    // 1. Freeze service registry for zero-lock hot-path lookups across all worker threads
    services_->freeze();

    // 2. Run asynchronous startup lifecycle hooks once across the cluster
    for (const auto& hook : startup_hooks_) {
        core::EventLoop init_loop;
        init_loop.spawn(hook(*this));
        init_loop.run();
    }

    std::vector<ListenerConfig> active_listeners = listeners_;
    if (active_listeners.empty()) {
        active_listeners.push_back(ListenerConfig{port_, host_, tls_enabled_});
    } else if (active_listeners.size() == 1 && tls_enabled_ && !active_listeners[0].tls) {
        active_listeners[0].tls = true;
    }

    workers_.clear();

    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this, i, active_listeners]() {
            try {
                struct ActiveListener {
                    int fd;
                    uint16_t port;
                    bool is_tls;
                };
                std::vector<ActiveListener> thread_listeners;
                for (const auto& l : active_listeners) {
                    int fd = create_listen_socket(l.port, l.host);
                    thread_listeners.push_back({fd, l.port, l.tls});
                }

                core::IoUringConfig ring_cfg;
                ring_cfg.entries = ring_entries_;
                ring_cfg.enable_sqpoll = sqpoll_enabled_;
                ring_cfg.sq_thread_idle_ms = sq_thread_idle_ms_;
                ring_cfg.sq_thread_cpu = sq_thread_cpu_ >= 0 ? sq_thread_cpu_ : static_cast<int>(i);

                core::EventLoop loop(ring_cfg, buffer_pool_entries_, 4096);
                cpu_set_t current_mask;
                if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &current_mask) == 0) {
                    std::vector<int> allowed;
                    for (int c = 0; c < CPU_SETSIZE; ++c) {
                        if (CPU_ISSET(c, &current_mask)) allowed.push_back(c);
                    }
                    if (i < allowed.size()) {
                        loop.pin_to_core(allowed[i]);
                    }
                }
                for (const auto& al : thread_listeners) {
                    loop.spawn(accept_loop(loop, al.fd, al.port, al.is_tls));
                }

                // Spawn background workers on core 0
                if (i == 0) {
                    for (const auto& worker : background_workers_) {
                        loop.spawn(worker(*this, loop));
                    }
                }

                std::vector<std::unique_ptr<v3::Http3Server>> h3_workers;
                if (tls_enabled_ && http3_enabled_ && tls_ctx_) {
                    for (const auto& l : active_listeners) {
                        if (l.tls) {
                            auto h3 = std::make_unique<v3::Http3Server>(loop, l.port, router_, tls_ctx_->native_handle(), services_.get());
                            if (h3->start()) {
                                loop.spawn(h3->run_receive_loop());
                                loop.spawn(h3->run_timer_loop());
                                h3_workers.push_back(std::move(h3));
                            }
                        }
                    }
                }

                loop.run();

                for (auto& h3 : h3_workers) {
                    if (h3) h3->stop();
                }
                for (const auto& al : thread_listeners) {
                    close(al.fd);
                }
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

    // Run asynchronous shutdown lifecycle hooks
    for (const auto& hook : shutdown_hooks_) {
        core::EventLoop stop_loop;
        stop_loop.spawn(hook(*this));
        stop_loop.run();
    }

    for (auto& h3 : h3_servers_) {
        if (h3) h3->stop();
    }
    h3_servers_.clear();
}

} // namespace aegon::http
