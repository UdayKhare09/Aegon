#include "http/client/ConnectionPool.h"
#include <cstring>
#include <algorithm>

namespace aegon::http::client {

// -----------------------------------------------------------------------------
// PooledConnection Implementation
// -----------------------------------------------------------------------------

PooledConnection::PooledConnection(core::EventLoop& loop, core::os_socket_t fd, std::string origin,
                                   std::unique_ptr<TlsClientStream> tls)
    : loop_(&loop), engine_id_(loop.engine().id()), fd_(fd), origin_(std::move(origin)), tls_stream_(std::move(tls)),
      last_used(std::chrono::steady_clock::now()) {}

PooledConnection::~PooledConnection() {
    close();
}

PooledConnection::PooledConnection(PooledConnection&& other) noexcept
    : loop_(other.loop_), engine_id_(other.engine_id_), fd_(other.fd_), origin_(std::move(other.origin_)),
      tls_stream_(std::move(other.tls_stream_)), is_closed_(other.is_closed_),
      last_used(other.last_used) {
    other.fd_ = core::INVALID_OS_SOCKET;
    other.is_closed_ = true;
}

PooledConnection& PooledConnection::operator=(PooledConnection&& other) noexcept {
    if (this != &other) {
        close();
        loop_ = other.loop_;
        engine_id_ = other.engine_id_;
        fd_ = other.fd_;
        origin_ = std::move(other.origin_);
        tls_stream_ = std::move(other.tls_stream_);
        is_closed_ = other.is_closed_;
        last_used = other.last_used;
        other.fd_ = core::INVALID_OS_SOCKET;
        other.is_closed_ = true;
    }
    return *this;
}

void PooledConnection::close() noexcept {
    if (core::is_valid_socket(fd_) && !is_closed_) {
        tls_stream_.reset();
        core::close_socket(fd_);
        fd_ = core::INVALID_OS_SOCKET;
        is_closed_ = true;
    }
}

core::Task<int> PooledConnection::write(const void* buf, size_t len) {
    if (is_closed_ || !core::is_valid_socket(fd_)) co_return -1;
    if (tls_stream_) {
        co_return co_await tls_stream_->write_plaintext(buf, len);
    }

    int n = co_await loop_->engine().send(
        fd_, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(buf), len));
    co_return n;
}

core::Task<int> PooledConnection::read(void* buf, size_t max_len) {
    if (is_closed_ || !core::is_valid_socket(fd_)) co_return -1;
    if (tls_stream_) {
        co_return co_await tls_stream_->read_plaintext(buf, max_len);
    }

    int n = co_await loop_->engine().recv(fd_, buf, max_len, 0);
    co_return n;
}

// -----------------------------------------------------------------------------
// ConnectionPool Implementation
// -----------------------------------------------------------------------------

ConnectionPool::ConnectionPool(PoolConfig config, TlsClientOptions tls_options)
    : config_(config), tls_options_(tls_options), tls_ctx_(tls_options) {}

ConnectionPool::~ConnectionPool() {
    clear();
}

void ConnectionPool::clear() {
    for (auto& [origin, list] : idle_conns_) {
        for (auto& conn : list) {
            if (conn) conn->close();
        }
        list.clear();
    }
    idle_conns_.clear();
}

size_t ConnectionPool::idle_count() const noexcept {
    size_t count = 0;
    for (const auto& [origin, list] : idle_conns_) {
        count += list.size();
    }
    return count;
}

void ConnectionPool::release(std::unique_ptr<PooledConnection> conn, bool keep_alive) {
    if (!conn) return;

    if (!keep_alive || !conn->is_valid() || idle_count() >= config_.max_idle_connections) {
        conn->close();
        return;
    }

    auto& list = idle_conns_[conn->origin()];
    if (list.size() >= config_.max_connections_per_host) {
        conn->close();
        return;
    }

    conn->last_used = std::chrono::steady_clock::now();
    list.push_back(std::move(conn));
}

core::Task<std::expected<std::unique_ptr<PooledConnection>, ConnectionError>>
ConnectionPool::acquire(const Url& url, core::EventLoop& loop) {
    std::string origin = url.origin();
    auto it = idle_conns_.find(origin);

    if (it != idle_conns_.end() && !it->second.empty()) {
        auto now = std::chrono::steady_clock::now();
        while (!it->second.empty()) {
            auto conn = std::move(it->second.back());
            it->second.pop_back();

            if (conn && conn->is_valid()) {
                if (now - conn->last_used <= config_.idle_timeout) {
#if defined(_WIN32)
                    // On Windows IOCP, an OS socket handle is permanently bound to the IOCP port
                    // it was associated with at creation. If acquired by a different event loop engine,
                    // it cannot be migrated; close it and create a fresh one for the current engine.
                    if (conn->engine_id_ != loop.engine().id()) {
                        conn->close();
                        continue;
                    }
#endif
                    conn->set_loop(loop);
                    co_return std::move(conn);
                }
                conn->close(); // Expired idle connection
            }
        }
    }

    co_return co_await create_connection(url, loop);
}

core::Task<std::expected<std::unique_ptr<PooledConnection>, ConnectionError>>
ConnectionPool::create_connection(const Url& url, core::EventLoop& loop) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = nullptr;
    std::string host_str(url.host());
    std::string port_str = std::to_string(url.port());

    int rc = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        co_return std::unexpected(ConnectionError::DnsLookupFailed);
    }

#if defined(_WIN32)
    core::os_socket_t fd = WSASocketW(res->ai_family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (!core::is_valid_socket(fd)) {
        ::freeaddrinfo(res);
        co_return std::unexpected(ConnectionError::SocketCreationFailed);
    }
#else
    core::os_socket_t fd = ::socket(res->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (!core::is_valid_socket(fd)) {
        ::freeaddrinfo(res);
        co_return std::unexpected(ConnectionError::SocketCreationFailed);
    }
#endif

    int flag = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

    int connect_rc = co_await loop.engine().connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen));
    ::freeaddrinfo(res);

    if (connect_rc < 0 && connect_rc != -EINPROGRESS) {
        core::close_socket(fd);
        co_return std::unexpected(ConnectionError::ConnectFailed);
    }

    std::unique_ptr<TlsClientStream> tls_stream = nullptr;
    if (url.is_https()) {
        if (!tls_ctx_.is_valid()) {
            core::close_socket(fd);
            co_return std::unexpected(ConnectionError::TlsHandshakeFailed);
        }

        tls_stream = std::make_unique<TlsClientStream>(
            loop, fd, tls_ctx_.native_handle(), url.host(), tls_options_);

        bool tls_ok = co_await tls_stream->handshake();
        if (!tls_ok) {
            core::close_socket(fd);
            co_return std::unexpected(ConnectionError::TlsHandshakeFailed);
        }
    }

    co_return std::make_unique<PooledConnection>(loop, fd, url.origin(), std::move(tls_stream));
}

} // namespace aegon::http::client
