#include "core/engine/IoEngine.h"
#include "core/BufferPool.h"
#include "log/Logger.h"
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace aegon::core {

// ---------------------------------------------------------------------------
// Awaiter await_resume implementations
// ---------------------------------------------------------------------------

AcceptResult AcceptAwaiter::await_resume() noexcept {
    AcceptResult res;
    res.fd       = result;
    res.addr     = addr;
    res.addr_len = addr_len;
    return res;
}

RecvResult RecvProvidedAwaiter::await_resume() noexcept {
    RecvResult res;
    res.bytes        = result;
    res.buffer_valid = (flags & 0x01 /* IORING_CQE_F_BUFFER */) != 0 || (result > 0 && (flags & 0x80000000));
    if (res.buffer_valid) {
        res.bid = static_cast<uint16_t>(flags >> 16);
    }
    res.has_more = (flags & 0x02 /* IORING_CQE_F_MORE */) != 0;
    res.eof      = (result == 0);
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
    return result;
}

// ---------------------------------------------------------------------------
// MultishotAcceptStream
// ---------------------------------------------------------------------------

MultishotAcceptStream::StreamState::~StreamState() {
    if (armed) {
        engine.cancel_multishot_accept(*this);
    }
}

void MultishotAcceptStream::StreamState::on_completion(int res, uint32_t cqe_flags) noexcept {
    AcceptResult r;
    r.fd           = res;
    r.has_more     = (cqe_flags & 0x02 /* IORING_CQE_F_MORE */) != 0;
    r.is_direct_fd = use_direct;

    bool kernel_disarmed = !r.has_more && res >= 0;
    if (kernel_disarmed) {
        armed = false;
    } else {
        armed = r.has_more;
    }

    if (res < 0) {
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

MultishotAcceptStream::MultishotAcceptStream(IoEngine& engine, int listen_fd, bool use_direct)
    : state_(std::make_shared<StreamState>(engine, listen_fd, use_direct)) {
    state_->self_ = state_;
}

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
    r.buffer_valid = (cqe_flags & 0x01 /* IORING_CQE_F_BUFFER */) != 0 || (res > 0 && (cqe_flags & 0x80000000));
    if (r.buffer_valid) {
        r.bid = static_cast<uint16_t>(cqe_flags >> 16);
    }
    r.has_more = (cqe_flags & 0x02 /* IORING_CQE_F_MORE */) != 0;
    r.eof      = (res == 0);

    bool kernel_disarmed = !r.has_more && res > 0;
    if (kernel_disarmed) {
        armed = false;
    } else {
        armed = r.has_more;
    }

    if (res <= 0) {
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

MultishotRecvStream::MultishotRecvStream(IoEngine& engine, int socket_fd, uint16_t bgid)
    : state_(std::make_shared<StreamState>(engine, socket_fd, bgid)) {
    state_->self_ = state_;
}

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

Task<int> IoEngine::send_all(int fd, const void* buf, size_t len) {
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

Task<int> IoEngine::send_all(int fd, const char* str) {
    co_return co_await send_all(fd, str, std::strlen(str));
}

Task<int> IoEngine::send_all(int fd, std::string_view data) {
    co_return co_await send_all(fd, data.data(), data.size());
}

Task<int> IoEngine::send_all(int fd, std::span<const uint8_t> data) {
    co_return co_await send_all(fd, data.data(), data.size());
}

} // namespace aegon::core

#include "core/engine/IoUringEngine.h"
#include "core/engine/EpollEngine.h"

namespace aegon::core {

static bool should_use_epoll(const EngineConfig& config) noexcept {
    if (config.type == EngineType::Epoll) return true;
    if (config.type == EngineType::IoUring) return false;
    const char* env = std::getenv("AEGON_ENGINE");
    if (env && (std::strcmp(env, "epoll") == 0 || std::strcmp(env, "EPOLL") == 0)) {
        return true;
    }
    return false;
}

std::unique_ptr<IoEngine> IoEngine::create(const EngineConfig& config) {
#if defined(__linux__)
    if (should_use_epoll(config)) {
        return std::make_unique<EpollEngine>(config, nullptr);
    }
    if (config.type == EngineType::Auto) {
        try {
            return std::make_unique<IoUringEngine>(config, nullptr);
        } catch (const std::exception& e) {
            log::warn("[IoEngine] io_uring init failed ({}). Falling back to epoll engine.", e.what());
            return std::make_unique<EpollEngine>(config, nullptr);
        }
    }
    return std::make_unique<IoUringEngine>(config, nullptr);
#else
    (void)config;
    throw std::runtime_error("Unsupported platform engine");
#endif
}

std::unique_ptr<IoEngine> IoEngine::create(const EngineConfig& config, BufferPool& buffer_pool) {
#if defined(__linux__)
    if (should_use_epoll(config)) {
        return std::make_unique<EpollEngine>(config, &buffer_pool);
    }
    if (config.type == EngineType::Auto) {
        try {
            return std::make_unique<IoUringEngine>(config, &buffer_pool);
        } catch (const std::exception& e) {
            log::warn("[IoEngine] io_uring init failed ({}). Falling back to epoll engine.", e.what());
            return std::make_unique<EpollEngine>(config, &buffer_pool);
        }
    }
    return std::make_unique<IoUringEngine>(config, &buffer_pool);
#else
    (void)config;
    (void)buffer_pool;
    throw std::runtime_error("Unsupported platform engine");
#endif
}

} // namespace aegon::core

