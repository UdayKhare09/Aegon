#include "http/Server.h"
#include "http/v1/Http1Connection.h"
#include "http/v2/Http2Connection.h"
#include "http/v2/Http2Frame.h"
#include "http/tls/TlsContext.h"
#include "http/tls/TlsStream.h"
#include "http/v3/Http3Server.h"
#include "log/Logger.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cerrno>
#include <csignal>

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
      config_(other.config_),
      tls_enabled_(other.tls_enabled_),
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
        config_ = other.config_;
        tls_enabled_ = other.tls_enabled_;
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
    if (config_.tcp.nodelay) {
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    }

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

core::Task<void> Server::run_h2_loop(core::EventLoop& loop, int client_fd, v2::Http2Connection& h2,
                                     std::string initial_data,
                                     std::optional<core::MultishotRecvStream> existing_stream) {
    if (!initial_data.empty()) {
        bool ok = co_await h2.feed_data(initial_data.data(), initial_data.size());
        if (!ok) {
            (void)(co_await loop.ring().close(client_fd));
            co_return;
        }
    }

    auto stream = existing_stream.has_value()
        ? std::move(*existing_stream)
        : loop.ring().recv_multishot_stream(client_fd, loop.buffer_pool().bgid());
    while (running_ && !h2.is_closed() && h2.wants_read()) {
        auto recv_res = co_await stream.next();
        if (recv_res.bytes <= 0) {
            break;
        }

        auto buf_slice = loop.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        bool ok = co_await h2.feed_data(buf_slice.data(), buf_slice.size());
        loop.buffer_pool().return_buffer(recv_res.bid);

        if (!ok) {
            break;
        }
    }

    (void)(co_await loop.ring().shutdown(client_fd, SHUT_WR));
    (void)(co_await loop.ring().close(client_fd));
}

core::Task<void> Server::handle_http2_connection(core::EventLoop& loop, int client_fd, std::string initial_data,
                                                  std::optional<core::MultishotRecvStream> existing_stream) {
    v2::Http2Connection h2(loop, client_fd, router_, services_.get(), nullptr, config_);
    bool ok = co_await h2.init();
    if (!ok) {
        (void)(co_await loop.ring().close(client_fd));
        co_return;
    }

    co_await run_h2_loop(loop, client_fd, h2, std::move(initial_data), std::move(existing_stream));
}

core::Task<void> Server::handle_http2_upgrade(core::EventLoop& loop, int client_fd, Request req, std::string http2_settings,
                                              std::string initial_data,
                                              std::optional<core::MultishotRecvStream> existing_stream) {
    v2::Http2Connection h2(loop, client_fd, router_, services_.get(), nullptr, config_);
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

    co_await run_h2_loop(loop, client_fd, h2, std::move(initial_data), std::move(existing_stream));
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

        v2::Http2Connection h2(loop, client_fd, router_, services_.get(), std::move(sender), config_);
        if (config_.enable_http3) {
            h2.set_alt_svc("h3=\":" + std::to_string(port) + "\"; ma=86400");
        }
        ok = co_await h2.init();
        if (!ok) {
            (void)(co_await loop.ring().close(client_fd));
            co_return;
        }

        char read_buf[4096];
        while (running_ && !h2.is_closed() && h2.wants_read()) {
            int n = co_await tls_stream.read_plaintext(read_buf, sizeof(read_buf));
            if (n <= 0) break;

            ok = co_await h2.feed_data(read_buf, static_cast<size_t>(n));
            if (!ok) break;
        }
    } else {
        // HTTP/1.1 over TLS (ALPN "http/1.1" or fallback)
        const std::string alt_svc_hdr = v1::Http1Connection::build_alt_svc_header(port, config_.enable_http3);
        v1::Http1Connection h1(loop, client_fd, router_, services_.get(), config_);
        co_await h1.run_tls(tls_stream, alt_svc_hdr);
    }

    (void)(co_await loop.ring().shutdown(client_fd, SHUT_WR));
    (void)(co_await loop.ring().close(client_fd));
}

core::Task<void> Server::handle_connection(core::EventLoop& loop, int client_fd) {
    v1::Http1Connection h1(loop, client_fd, router_, services_.get(), config_);
    co_await h1.run(
        [this, &loop](int fd, std::string preface_data) -> core::Task<void> {
            co_await handle_http2_connection(loop, fd, std::move(preface_data));
        },
        [this, &loop](int fd, Request req, std::string settings, std::string trailing) -> core::Task<void> {
            co_await handle_http2_upgrade(loop, fd, std::move(req), std::move(settings), std::move(trailing));
        }
    );
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
            if (config_.tcp.nodelay) {
                int nodelay = 1;
                ::setsockopt(accept_res.fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
            }
            if (config_.tcp.keepalive) {
                int keepalive = 1;
                ::setsockopt(accept_res.fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
                if (config_.tcp.keepidle > 0) {
                    int keepidle = config_.tcp.keepidle;
                    ::setsockopt(accept_res.fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
                }
                if (config_.tcp.keepintvl > 0) {
                    int keepintvl = config_.tcp.keepintvl;
                    ::setsockopt(accept_res.fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
                }
                if (config_.tcp.keepcnt > 0) {
                    int keepcnt = config_.tcp.keepcnt;
                    ::setsockopt(accept_res.fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
                }
            }
            if (is_tls && tls_ctx_) {
                loop.spawn(handle_tls_connection(loop, accept_res.fd, port));
            } else {
                loop.spawn(handle_connection(loop, accept_res.fd));
            }
        }
    }
}

void Server::run_event_loop(const std::vector<ListenerConfig>& active_listeners,
                            std::optional<int> cpu_core,
                            bool spawn_background) {
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
    ring_cfg.entries = config_.ring_entries;

    core::EventLoop loop(ring_cfg, config_.buffer_pool_entries, config_.buffer_size);
    if (cpu_core) {
        loop.pin_to_core(*cpu_core);
    }
    for (const auto& al : thread_listeners) {
        loop.spawn(accept_loop(loop, al.fd, al.port, al.is_tls));
    }

    if (spawn_background) {
        for (const auto& worker : background_workers_) {
            loop.spawn(worker(*this, loop));
        }
    }

    std::vector<std::unique_ptr<v3::Http3Server>> local_h3_servers;
    auto& h3_dest = cpu_core.has_value() ? local_h3_servers : h3_servers_;
    if (tls_enabled_ && config_.enable_http3 && tls_ctx_) {
        for (const auto& l : active_listeners) {
            if (l.tls) {
                auto h3 = std::make_unique<v3::Http3Server>(loop, l.port, router_, tls_ctx_->native_handle(), services_.get(), config_);
                if (h3->start()) {
                    loop.spawn(h3->run_receive_loop());
                    loop.spawn(h3->run_timer_loop());
                    h3_dest.push_back(std::move(h3));
                }
            }
        }
    }

    loop.run();

    for (auto& h3 : h3_dest) {
        if (h3) h3->stop();
    }
    if (!cpu_core.has_value()) {
        h3_servers_.clear();
    }
    for (const auto& al : thread_listeners) {
        close(al.fd);
    }
}

void Server::run() {
    ::signal(SIGPIPE, SIG_IGN);
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

    for (const auto& l : active_listeners) {
        log::info("Aegon HTTP{} listening on {}:{} [workers=1]",
                  l.tls ? "S" : "", l.host, l.port);
    }

    run_event_loop(active_listeners, std::nullopt, true);
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

    for (const auto& l : active_listeners) {
        log::info("Aegon HTTP{} listening on {}:{} [workers={}]",
                  l.tls ? "S" : "", l.host, l.port, threads);
    }

    workers_.clear();

    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this, i, active_listeners]() {
            try {
                std::optional<int> core_id;
                cpu_set_t current_mask;
                if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &current_mask) == 0) {
                    std::vector<int> allowed;
                    for (int c = 0; c < CPU_SETSIZE; ++c) {
                        if (CPU_ISSET(c, &current_mask)) allowed.push_back(c);
                    }
                    if (i < allowed.size()) {
                        core_id = allowed[i];
                    }
                }

                run_event_loop(active_listeners, core_id, i == 0);
            } catch (const std::exception& e) {
                log::error("Worker thread {} error: {}", i, e.what());
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
    log::info("Aegon server stopping");

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
