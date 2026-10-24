#include "core/engine/IoEngine.h"
#include "core/BufferPool.h"
#include "log/Logger.h"
#include <cstring>
#include <cerrno>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace aegon::core {

// ---------------------------------------------------------------------------
// Awaiter await_resume implementations
// ---------------------------------------------------------------------------

AcceptResult AcceptAwaiter::await_resume() noexcept {
    AcceptResult res;
#if defined(_WIN32)
    if (result >= 0 && client_sock != INVALID_OS_SOCKET) {
        ::setsockopt(client_sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                     reinterpret_cast<const char*>(&listen_fd), sizeof(listen_fd));
        res.fd = client_sock;
    } else {
        res.fd = INVALID_OS_SOCKET;
    }
    res.addr     = addr;
    res.addr_len = addr_len;
#else
    res.fd       = result;
    res.addr     = addr;
    res.addr_len = addr_len;
#endif
    return res;
}

RecvResult RecvProvidedAwaiter::await_resume() noexcept {
    RecvResult res;
    res.bytes        = result;
#if defined(_WIN32)
    res.buffer_valid = (result >= 0);
    res.bid          = allocated_bid;
    res.has_more     = false;
    res.eof          = (result == 0);
#else
    res.buffer_valid = (flags & 0x01 /* IORING_CQE_F_BUFFER */) != 0 || (result > 0 && (flags & 0x80000000));
    if (res.buffer_valid) {
        res.bid = static_cast<uint16_t>(flags >> 16);
    }
    res.has_more = (flags & 0x02 /* IORING_CQE_F_MORE */) != 0;
    res.eof      = (result == 0);
#endif
    return res;
}

int RecvAwaiter::await_resume() noexcept {
    return result;
}

int SendAwaiter::await_resume() noexcept {
    return result;
}

void SendZcAwaiter::on_completion(int res, uint32_t cqe_flags) noexcept {
    if (res < 0) {
        result = res;
        flags  = cqe_flags;
        IoEngine::resume_continuation(*this);
        return;
    }

    if (cqe_flags & 0x08 /* IORING_CQE_F_NOTIF */) {
        notif_received = true;
    } else {
        bytes_sent     = res;
        send_completed = true;
        if (!(cqe_flags & 0x02 /* IORING_CQE_F_MORE */)) {
            notif_received = true;
        }
    }

    if (send_completed && notif_received) {
        result = bytes_sent;
        flags  = cqe_flags;
        IoEngine::resume_continuation(*this);
    }
}

int SendZcAwaiter::await_resume() noexcept {
    return result;
}

int SpliceAwaiter::await_resume() noexcept {
    return result;
}

int ConnectAwaiter::await_resume() noexcept {
#if defined(_WIN32)
    if (result == 0 && socket_fd != INVALID_OS_SOCKET) {
        ::setsockopt(socket_fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
    }
#endif
    return result;
}

int PollAwaiter::await_resume() noexcept {
    return result;
}

int TimeoutAwaiter::await_resume() noexcept {
    return result;
}

int CloseAwaiter::await_resume() noexcept {
    return result;
}

int ShutdownAwaiter::await_resume() noexcept {
    return result;
}

int CancelAwaiter::await_resume() noexcept {
    return result;
}

int RecvmsgAwaiter::await_resume() noexcept {
#if defined(_WIN32)
    if (msg && result >= 0) {
        msg->msg_namelen = static_cast<socklen_t>(from_len);
    }
#endif
    return result;
}

// ---------------------------------------------------------------------------
// MultishotAcceptStream
// ---------------------------------------------------------------------------

MultishotAcceptStream::StreamState::~StreamState() {
    if (armed) {
        engine.cancel_multishot_accept(*this);
    }
#if defined(_WIN32)
    if (client_sock != INVALID_OS_SOCKET) {
        core::close_socket(client_sock);
        client_sock = INVALID_OS_SOCKET;
    }
#endif
}

void MultishotAcceptStream::StreamState::on_completion(int res, uint32_t cqe_flags) noexcept {
    AcceptResult r;
#if defined(_WIN32)
    if (res >= 0 && client_sock != INVALID_OS_SOCKET) {
        ::setsockopt(client_sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                     reinterpret_cast<const char*>(&listen_fd), sizeof(listen_fd));
        r.fd = client_sock;
        client_sock = INVALID_OS_SOCKET;
    } else {
        r.fd = (res < 0) ? res : -1;
        if (client_sock != INVALID_OS_SOCKET) {
            core::close_socket(client_sock);
            client_sock = INVALID_OS_SOCKET;
        }
    }
    r.has_more = false;
    r.is_direct_fd = false;
#else
    r.fd           = res;
    r.has_more     = (cqe_flags & 0x02 /* IORING_CQE_F_MORE */) != 0;
    r.is_direct_fd = use_direct;
#endif

    bool kernel_disarmed = !r.has_more && res >= 0;
    if (kernel_disarmed) {
        armed = false;
    } else {
        armed = r.has_more;
    }

    if (!r.has_more || res < 0) {
        finished = true;
        armed    = false;
    }

    if (continuation && !continuation.done()) {
        current_result = r;
        auto cont = continuation;
        continuation = nullptr;
        cont.resume();
    } else {
        backlog.push_back(r);
    }
}

MultishotAcceptStream::MultishotAcceptStream(IoEngine& engine, os_socket_t listen_fd, bool use_direct)
    : state_(std::make_shared<StreamState>(engine, listen_fd, use_direct)) {}

MultishotAcceptStream::~MultishotAcceptStream() {
    cancel();
}

MultishotAcceptStream::MultishotAcceptStream(MultishotAcceptStream&&) noexcept = default;
MultishotAcceptStream& MultishotAcceptStream::operator=(MultishotAcceptStream&&) noexcept = default;

void MultishotAcceptStream::cancel() noexcept {
    if (state_ && state_->armed) {
        state_->engine.cancel_multishot_accept(*state_);
        state_->armed     = false;
        state_->cancelled = true;
        state_->finished  = true;
    }
}

// ---------------------------------------------------------------------------
// MultishotRecvStream
// ---------------------------------------------------------------------------

MultishotRecvStream::StreamState::~StreamState() {
    if (armed) {
        engine.cancel_multishot_recv(*this);
    }
}

void MultishotRecvStream::StreamState::on_completion(int res, uint32_t cqe_flags) noexcept {
    RecvResult r;
    r.bytes        = res;
#if defined(_WIN32)
    r.buffer_valid = (res >= 0);
    r.bid          = allocated_bid;
    r.has_more     = false;
    r.eof          = (res == 0);
#else
    r.buffer_valid = (cqe_flags & 0x01 /* IORING_CQE_F_BUFFER */) != 0 || (res > 0 && (cqe_flags & 0x80000000));
    if (r.buffer_valid) {
        r.bid = static_cast<uint16_t>(cqe_flags >> 16);
    }
    r.has_more = (cqe_flags & 0x02 /* IORING_CQE_F_MORE */) != 0;
    r.eof      = (res == 0);
#endif

    bool kernel_disarmed = !r.has_more && res > 0;
    if (kernel_disarmed) {
        armed = false;
    } else {
        armed = r.has_more;
    }

    if (!r.has_more || res <= 0) {
        finished = true;
        armed    = false;
    }

    if (continuation && !continuation.done()) {
        current_result = r;
        auto cont = continuation;
        continuation = nullptr;
        cont.resume();
    } else {
        backlog.push_back(r);
    }
}

MultishotRecvStream::MultishotRecvStream(IoEngine& engine, os_socket_t socket_fd, uint16_t bgid)
    : state_(std::make_shared<StreamState>(engine, socket_fd, bgid)) {}

MultishotRecvStream::~MultishotRecvStream() {
    cancel();
}

MultishotRecvStream::MultishotRecvStream(MultishotRecvStream&&) noexcept = default;
MultishotRecvStream& MultishotRecvStream::operator=(MultishotRecvStream&&) noexcept = default;

void MultishotRecvStream::cancel() noexcept {
    if (state_ && state_->armed) {
        state_->engine.cancel_multishot_recv(*state_);
        state_->armed     = false;
        state_->cancelled = true;
        state_->finished  = true;
    }
}

// ---------------------------------------------------------------------------
// send_all implementations
// ---------------------------------------------------------------------------

Task<int> IoEngine::send_all(os_socket_t fd, const void* buf, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        int sent = co_await send(fd, std::span<const uint8_t>(p, remaining));
        if (sent <= 0) {
            co_return (sent < 0) ? sent : static_cast<int>(len - remaining);
        }
        p += sent;
        remaining -= static_cast<size_t>(sent);
    }
    co_return static_cast<int>(len);
}

Task<int> IoEngine::send_all(os_socket_t fd, const char* str) {
    co_return co_await send_all(fd, str, std::strlen(str));
}

Task<int> IoEngine::send_all(os_socket_t fd, std::string_view data) {
    co_return co_await send_all(fd, data.data(), data.size());
}

Task<int> IoEngine::send_all(os_socket_t fd, std::span<const uint8_t> data) {
    co_return co_await send_all(fd, data.data(), data.size());
}

} // namespace aegon::core

#if defined(_WIN32)
#include "core/engine/IocpEngine.h"
#elif defined(__linux__)
#include "core/engine/IoUringEngine.h"
#include "core/engine/EpollEngine.h"
#endif

namespace aegon::core {

#if defined(__linux__)
enum class EffectiveEngineTarget {
    ExplicitEpoll,
    ExplicitIoUring,
    AutoProbe
};

static EffectiveEngineTarget resolve_engine_target(const EngineConfig& config) noexcept {
    if (config.type == EngineType::Epoll) return EffectiveEngineTarget::ExplicitEpoll;
    if (config.type == EngineType::IoUring) return EffectiveEngineTarget::ExplicitIoUring;

    const char* env = std::getenv("AEGON_ENGINE");
    if (env) {
        if (std::strcmp(env, "epoll") == 0 || std::strcmp(env, "EPOLL") == 0) {
            return EffectiveEngineTarget::ExplicitEpoll;
        }
        if (std::strcmp(env, "io_uring") == 0 || std::strcmp(env, "IO_URING") == 0 ||
            std::strcmp(env, "uring") == 0 || std::strcmp(env, "URING") == 0) {
            return EffectiveEngineTarget::ExplicitIoUring;
        }
    }
    return EffectiveEngineTarget::AutoProbe;
}
#endif

std::unique_ptr<IoEngine> IoEngine::create(const EngineConfig& config) {
#if defined(_WIN32)
    return std::make_unique<IocpEngine>(config, nullptr);
#elif defined(__linux__)
    auto target = resolve_engine_target(config);
    if (target == EffectiveEngineTarget::ExplicitEpoll) {
        return std::make_unique<EpollEngine>(config, nullptr);
    }
    if (target == EffectiveEngineTarget::ExplicitIoUring) {
        // Explicitly chosen either programmatically or via AEGON_ENGINE=io_uring:
        // Must fail fast and throw without falling back if initialization fails.
        return std::make_unique<IoUringEngine>(config, nullptr);
    }
    // AutoProbe: attempt IoUringEngine, fall back to EpollEngine on failure
    try {
        return std::make_unique<IoUringEngine>(config, nullptr);
    } catch (const std::exception& e) {
        log::warn("[IoEngine] io_uring init failed ({}). Falling back to epoll engine.", e.what());
        return std::make_unique<EpollEngine>(config, nullptr);
    }
#else
    (void)config;
    throw std::runtime_error("Unsupported platform engine");
#endif
}

std::unique_ptr<IoEngine> IoEngine::create(const EngineConfig& config, BufferPool& buffer_pool) {
#if defined(_WIN32)
    return std::make_unique<IocpEngine>(config, &buffer_pool);
#elif defined(__linux__)
    auto target = resolve_engine_target(config);
    if (target == EffectiveEngineTarget::ExplicitEpoll) {
        return std::make_unique<EpollEngine>(config, &buffer_pool);
    }
    if (target == EffectiveEngineTarget::ExplicitIoUring) {
        // Explicitly chosen either programmatically or via AEGON_ENGINE=io_uring:
        // Must fail fast and throw without falling back if initialization fails.
        return std::make_unique<IoUringEngine>(config, &buffer_pool);
    }
    // AutoProbe: attempt IoUringEngine, fall back to EpollEngine on failure
    try {
        return std::make_unique<IoUringEngine>(config, &buffer_pool);
    } catch (const std::exception& e) {
        log::warn("[IoEngine] io_uring init failed ({}). Falling back to epoll engine.", e.what());
        return std::make_unique<EpollEngine>(config, &buffer_pool);
    }
#else
    (void)config;
    (void)buffer_pool;
    throw std::runtime_error("Unsupported platform engine");
#endif
}

} // namespace aegon::core

