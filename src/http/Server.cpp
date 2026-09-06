#include "http/Server.h"
#include "http/v1/Http1Parser.h"
#include "http/v2/Http2Connection.h"
#include "http/v2/Http2Frame.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>

namespace aegon::http {

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

core::Task<void> Server::handle_connection(core::EventLoop& loop, int client_fd) {
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
    loop.run();

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
                loop.run();
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
}

} // namespace aegon::http
