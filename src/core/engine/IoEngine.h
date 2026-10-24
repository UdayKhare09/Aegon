#pragma once

#include "core/Task.h"
#include "core/OsSocket.h"
#include <coroutine>
#include <cstdint>
#include <cstddef>
#include <span>
#include <string_view>
#include <deque>
#include <memory>
#include <algorithm>
#include <cstring>
#include <atomic>

namespace aegon::core {

class IoEngine;
class BufferPool;

/**
 * @brief Identifies the I/O engine backend implementation.
 */
enum class EngineType {
    Auto,     // Automatically probe and select the best available backend
    IoUring,  // Linux io_uring (Linux 6.1+ preferred)
    Epoll,    // Linux epoll (universal Linux fallback)
    Kqueue,   // macOS / BSD kqueue
    Iocp      // Windows I/O Completion Ports
};

/**
 * @brief Common configuration for I/O engine initialization.
 */
struct EngineConfig {
    EngineType type{EngineType::Auto};
    uint32_t   entries{4096};
    uint32_t   flags{0};
    unsigned   direct_fd_count{0};
    bool       prefer_io_uring{true};
};

/**
 * @brief Result of an accepted connection.
 */
struct AcceptResult {
    os_socket_t fd{INVALID_OS_SOCKET};
    sockaddr_storage addr{};
    socklen_t addr_len{sizeof(sockaddr_storage)};
    bool has_more{false};
    bool is_direct_fd{false};
};

/**
 * @brief Result of a buffer-provided network receive.
 */
struct RecvResult {
    int bytes{0};
    uint16_t bid{0};
    bool has_more{false};
    bool eof{false};
    bool buffer_valid{false};
};

/**
 * @brief Base class for asynchronous I/O operations across all engines.
 * Zero heap allocation: awaiters live on the coroutine stack frame.
 */
struct IoAwaiter {
#if defined(_WIN32)
    OVERLAPPED overlapped{};
#endif
    std::coroutine_handle<> continuation{nullptr};
    int32_t result{0};
    uint32_t flags{0};

    virtual ~IoAwaiter() = default;

    [[nodiscard]] virtual bool await_ready() const noexcept { return false; }

    virtual void submit() noexcept = 0;

    inline void await_suspend(std::coroutine_handle<> h) noexcept {
        continuation = h;
        submit();
    }

    virtual void on_completion(int res, uint32_t cqe_flags) noexcept {
        result = res;
        flags = cqe_flags;
        if (continuation && !continuation.done()) {
            continuation.resume();
        }
    }
};

// ---------------------------------------------------------------------------
// Awaiter structs
// ---------------------------------------------------------------------------

struct AcceptAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t listen_fd{INVALID_OS_SOCKET};
    sockaddr_storage addr{};
    socklen_t addr_len{sizeof(sockaddr_storage)};
#if defined(_WIN32)
    os_socket_t client_sock{INVALID_OS_SOCKET};
    alignas(8) uint8_t accept_buf[(sizeof(sockaddr_storage) + 16) * 2]{};
#endif

    AcceptAwaiter(IoEngine& e, os_socket_t fd) noexcept : engine(e), listen_fd(fd) {}
    void submit() noexcept override;
    [[nodiscard]] AcceptResult await_resume() noexcept;
};

struct RecvProvidedAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t socket_fd{INVALID_OS_SOCKET};
    uint16_t bgid{0};
#if defined(_WIN32)
    WSABUF wsa_buf{};
    uint16_t allocated_bid{0};
#endif

    RecvProvidedAwaiter(IoEngine& e, os_socket_t fd, uint16_t b) noexcept
        : engine(e), socket_fd(fd), bgid(b) {}
    void submit() noexcept override;
    [[nodiscard]] RecvResult await_resume() noexcept;
};

struct RecvAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t socket_fd{INVALID_OS_SOCKET};
    void* buf{nullptr};
    size_t len{0};
    int flags_arg{0};
#if defined(_WIN32)
    WSABUF wsa_buf{};
#endif

    RecvAwaiter(IoEngine& e, os_socket_t fd, void* b, size_t l, int fl = 0) noexcept
        : engine(e), socket_fd(fd), buf(b), len(l), flags_arg(fl) {}
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct SendAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t socket_fd{INVALID_OS_SOCKET};
    const void* buf{nullptr};
    size_t len{0};
#if defined(_WIN32)
    WSABUF wsa_buf{};
#endif

    SendAwaiter(IoEngine& e, os_socket_t fd, const void* b, size_t l) noexcept
        : engine(e), socket_fd(fd), buf(b), len(l) {}
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct SendZcAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t socket_fd{INVALID_OS_SOCKET};
    const void* buf{nullptr};
    size_t len{0};
    int zc_flags{0};
    int fixed_buf_idx{-1};
    int bytes_sent{0};
    bool send_completed{false};
    bool notif_received{false};
#if defined(_WIN32)
    WSABUF wsa_buf{};
#endif

    SendZcAwaiter(IoEngine& e, os_socket_t fd, const void* b, size_t l, int zc_fl = 0, int buf_idx = -1) noexcept
        : engine(e), socket_fd(fd), buf(b), len(l), zc_flags(zc_fl), fixed_buf_idx(buf_idx) {}
    void submit() noexcept override;
    void on_completion(int res, uint32_t flags) noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct SpliceAwaiter : IoAwaiter {
    IoEngine& engine;
    int fd_in;
    int64_t off_in;
    os_socket_t fd_out{INVALID_OS_SOCKET};
    int64_t off_out;
    unsigned int nbytes;
    unsigned int splice_flags;

    SpliceAwaiter(IoEngine& e, int in, int64_t o_in, os_socket_t out, int64_t o_out,
                  unsigned int bytes, unsigned int fl = 0) noexcept
        : engine(e), fd_in(in), off_in(o_in), fd_out(out), off_out(o_out),
          nbytes(bytes), splice_flags(fl) {}
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct ConnectAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t socket_fd{INVALID_OS_SOCKET};
    sockaddr_storage addr{};
    socklen_t addr_len{0};

    ConnectAwaiter(IoEngine& e, os_socket_t fd, const sockaddr* a, socklen_t l) noexcept
        : engine(e), socket_fd(fd), addr_len(std::min<socklen_t>(l, sizeof(sockaddr_storage))) {
        if (a && addr_len > 0) {
            std::memcpy(&addr, a, addr_len);
        }
    }
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct PollAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t fd{INVALID_OS_SOCKET};
    unsigned poll_mask;

    PollAwaiter(IoEngine& e, os_socket_t f, unsigned mask) noexcept
        : engine(e), fd(f), poll_mask(mask) {}
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct TimeoutAwaiter : IoAwaiter {
    IoEngine& engine;
    uint64_t ns{0};
    int64_t sec{0};
    int64_t nsec{0};

    TimeoutAwaiter(IoEngine& e, uint64_t n) noexcept
        : engine(e), ns(n),
          sec(static_cast<int64_t>(n / 1'000'000'000ULL)),
          nsec(static_cast<int64_t>(n % 1'000'000'000ULL)) {}
    void submit() noexcept override;
    int await_resume() noexcept;
};

struct CloseAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t fd{INVALID_OS_SOCKET};

    CloseAwaiter(IoEngine& e, os_socket_t f) noexcept : engine(e), fd(f) {}
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct ShutdownAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t fd{INVALID_OS_SOCKET};
    int how;

    ShutdownAwaiter(IoEngine& e, os_socket_t f, int h) noexcept : engine(e), fd(f), how(h) {}
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct CancelAwaiter : IoAwaiter {
    IoEngine& engine;
    uint64_t user_data;
    int cancel_flags;

    CancelAwaiter(IoEngine& e, uint64_t udata, int flags = 0) noexcept
        : engine(e), user_data(udata), cancel_flags(flags) {}
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

struct RecvmsgAwaiter : IoAwaiter {
    IoEngine& engine;
    os_socket_t socket_fd{INVALID_OS_SOCKET};
    msghdr* msg;
#if defined(_WIN32)
    WSABUF wsa_buf{};
    INT from_len{0};
#endif

    RecvmsgAwaiter(IoEngine& e, os_socket_t fd, msghdr* m) noexcept
        : engine(e), socket_fd(fd), msg(m) {}
    void submit() noexcept override;
    [[nodiscard]] int await_resume() noexcept;
};

// ---------------------------------------------------------------------------
// Multishot Stream Classes
// ---------------------------------------------------------------------------

class MultishotAcceptStream {
public:
    struct StreamState : IoAwaiter {
        IoEngine& engine;
        os_socket_t listen_fd{INVALID_OS_SOCKET};
        bool armed{false};
        bool finished{false};
        bool cancelled{false};
        bool use_direct{false};
        std::deque<AcceptResult> backlog{};
        AcceptResult current_result{};
#if defined(_WIN32)
        os_socket_t client_sock{INVALID_OS_SOCKET};
        alignas(8) uint8_t accept_buf[(sizeof(sockaddr_storage) + 16) * 2]{};
#endif

        StreamState(IoEngine& e, os_socket_t fd, bool direct = false) noexcept
            : engine(e), listen_fd(fd), use_direct(direct) {}
        ~StreamState();

        void submit() noexcept override;
        void on_completion(int res, uint32_t flags) noexcept override;
    };

    MultishotAcceptStream(IoEngine& engine, os_socket_t listen_fd, bool use_direct = false);
    ~MultishotAcceptStream();

    MultishotAcceptStream(const MultishotAcceptStream&) = delete;
    MultishotAcceptStream& operator=(const MultishotAcceptStream&) = delete;
    MultishotAcceptStream(MultishotAcceptStream&&) noexcept;
    MultishotAcceptStream& operator=(MultishotAcceptStream&&) noexcept;

    struct NextAwaiter {
        StreamState& state;

        bool await_ready() const noexcept {
            return !state.backlog.empty() || state.finished || state.cancelled;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            state.continuation = h;
            if (!state.armed && !state.finished && !state.cancelled) {
                state.submit();
            }
        }

        [[nodiscard]] AcceptResult await_resume() noexcept {
            if (!state.backlog.empty()) {
                auto res = state.backlog.front();
                state.backlog.pop_front();
                return res;
            }
            return state.current_result;
        }
    };

    [[nodiscard]] NextAwaiter next() noexcept {
        return NextAwaiter{*state_};
    }

    void cancel() noexcept;

    [[nodiscard]] bool is_armed()    const noexcept { return state_ && state_->armed; }
    [[nodiscard]] bool is_finished() const noexcept { return !state_ || state_->finished; }
    [[nodiscard]] os_socket_t listen_fd() const noexcept { return state_ ? state_->listen_fd : INVALID_OS_SOCKET; }

private:
    std::shared_ptr<StreamState> state_;
};

class MultishotRecvStream {
public:
    struct StreamState : IoAwaiter {
        IoEngine& engine;
        os_socket_t socket_fd{INVALID_OS_SOCKET};
        uint16_t bgid{0};
        bool armed{false};
        bool finished{false};
        bool cancelled{false};
        std::deque<RecvResult> backlog{};
        RecvResult current_result{};
#if defined(_WIN32)
        WSABUF wsa_buf{};
        uint16_t allocated_bid{0};
#endif

        StreamState(IoEngine& e, os_socket_t fd, uint16_t b) noexcept
            : engine(e), socket_fd(fd), bgid(b) {}
        ~StreamState();

        void submit() noexcept override;
        void on_completion(int res, uint32_t flags) noexcept override;
    };

    MultishotRecvStream(IoEngine& engine, os_socket_t socket_fd, uint16_t bgid);
    ~MultishotRecvStream();

    MultishotRecvStream(const MultishotRecvStream&) = delete;
    MultishotRecvStream& operator=(const MultishotRecvStream&) = delete;
    MultishotRecvStream(MultishotRecvStream&&) noexcept;
    MultishotRecvStream& operator=(MultishotRecvStream&&) noexcept;

    struct NextAwaiter {
        StreamState& state;

        bool await_ready() const noexcept {
            return !state.backlog.empty() || state.finished || state.cancelled;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            state.continuation = h;
            if (!state.armed && !state.finished && !state.cancelled) {
                state.submit();
            }
        }

        [[nodiscard]] RecvResult await_resume() noexcept {
            if (!state.backlog.empty()) {
                auto res = state.backlog.front();
                state.backlog.pop_front();
                return res;
            }
            if (state.cancelled) {
                return RecvResult{.bytes = -125 /* ECANCELED */};
            }
            if (state.finished) {
                RecvResult r = state.current_result;
                if (r.bytes == 0) r.eof = true;
                return r;
            }
            return state.current_result;
        }
    };

    [[nodiscard]] NextAwaiter next() noexcept {
        return NextAwaiter{*state_};
    }

    void cancel() noexcept;

    [[nodiscard]] bool is_armed()    const noexcept { return state_ && state_->armed; }
    [[nodiscard]] bool is_finished() const noexcept { return !state_ || state_->finished; }
    [[nodiscard]] os_socket_t socket_fd() const noexcept { return state_ ? state_->socket_fd : INVALID_OS_SOCKET; }

private:
    std::shared_ptr<StreamState> state_;
};

// ---------------------------------------------------------------------------
// IoEngine Pure Virtual Base Class
// ---------------------------------------------------------------------------

class IoEngine {
public:
    virtual ~IoEngine() = default;

    [[nodiscard]] virtual EngineType type() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Loop iteration
    virtual int submit_and_wait(uint32_t min_complete = 1) = 0;
    virtual size_t process_completions() noexcept = 0;
    virtual void wakeup() noexcept = 0;
    virtual void post_completion(IoAwaiter* aw, int res = 0) noexcept {
        (void)aw;
        (void)res;
    }

    // Low-level submit dispatchers invoked by awaiters
    virtual void submit_accept(AcceptAwaiter& aw) noexcept = 0;
    virtual void submit_recv_provided(RecvProvidedAwaiter& aw) noexcept = 0;
    virtual void submit_recv(RecvAwaiter& aw) noexcept = 0;
    virtual void submit_send(SendAwaiter& aw) noexcept = 0;
    virtual void submit_send_zc(SendZcAwaiter& aw) noexcept = 0;
    virtual void submit_splice(SpliceAwaiter& aw) noexcept = 0;
    virtual void submit_connect(ConnectAwaiter& aw) noexcept = 0;
    virtual void submit_poll(PollAwaiter& aw) noexcept = 0;
    virtual void submit_timeout(TimeoutAwaiter& aw) noexcept = 0;
    virtual void submit_close(CloseAwaiter& aw) noexcept = 0;
    virtual void submit_shutdown(ShutdownAwaiter& aw) noexcept = 0;
    virtual void submit_cancel(CancelAwaiter& aw) noexcept = 0;
    virtual void submit_recvmsg(RecvmsgAwaiter& aw) noexcept = 0;
    virtual void submit_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept = 0;
    virtual void cancel_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept = 0;
    virtual void submit_multishot_recv(MultishotRecvStream::StreamState& st) noexcept = 0;
    virtual void cancel_multishot_recv(MultishotRecvStream::StreamState& st) noexcept = 0;

    // Optional direct file registration (io_uring)
    virtual bool register_direct_fds(unsigned count) noexcept { (void)count; return false; }
    virtual void update_direct_fd(unsigned slot, int fd) { (void)slot; (void)fd; }
    virtual void remove_direct_fd(unsigned slot) noexcept { (void)slot; }
    [[nodiscard]] virtual bool has_direct_fds() const noexcept { return false; }
    [[nodiscard]] virtual unsigned direct_fd_slots() const noexcept { return 0; }
    virtual void register_buffer_pool(BufferPool& pool, bool register_buffers = true) { (void)pool; (void)register_buffers; }

    // High-level awaiter factory methods
    [[nodiscard]] AcceptAwaiter accept_once(os_socket_t listen_fd) noexcept {
        return AcceptAwaiter{*this, listen_fd};
    }

    [[nodiscard]] MultishotAcceptStream accept_multishot(os_socket_t listen_fd) noexcept {
        return MultishotAcceptStream{*this, listen_fd, false};
    }

    [[nodiscard]] virtual MultishotAcceptStream accept_multishot_direct(os_socket_t listen_fd) noexcept {
        return MultishotAcceptStream{*this, listen_fd, true};
    }

    [[nodiscard]] RecvProvidedAwaiter recv_provided(os_socket_t fd, uint16_t bgid) noexcept {
        return RecvProvidedAwaiter{*this, fd, bgid};
    }

    [[nodiscard]] MultishotRecvStream recv_multishot_stream(os_socket_t fd, uint16_t bgid) noexcept {
        return MultishotRecvStream{*this, fd, bgid};
    }

    [[nodiscard]] RecvAwaiter recv(os_socket_t fd, void* buf, size_t len, int flags = 0) noexcept {
        return RecvAwaiter{*this, fd, buf, len, flags};
    }

    [[nodiscard]] SendAwaiter send(os_socket_t fd, const void* buf, size_t len) noexcept {
        return SendAwaiter{*this, fd, buf, len};
    }

    [[nodiscard]] SendAwaiter send(os_socket_t fd, std::span<const uint8_t> data) noexcept {
        return SendAwaiter{*this, fd, data.data(), data.size()};
    }

    [[nodiscard]] SendAwaiter send(os_socket_t fd, std::string_view data) noexcept {
        return SendAwaiter{*this, fd, data.data(), data.size()};
    }

    [[nodiscard]] SendZcAwaiter send_zc(os_socket_t fd, std::span<const uint8_t> data, int zc_flags = 0) noexcept {
        return SendZcAwaiter{*this, fd, data.data(), data.size(), zc_flags, -1};
    }

    [[nodiscard]] SendZcAwaiter send_zc(os_socket_t fd, std::string_view data, int zc_flags = 0) noexcept {
        return SendZcAwaiter{*this, fd, data.data(), data.size(), zc_flags, -1};
    }

    [[nodiscard]] SendZcAwaiter send_zc_fixed(os_socket_t fd, const void* buf, size_t len,
                                              unsigned buf_index, int zc_flags = 0) noexcept {
        return SendZcAwaiter{*this, fd, buf, len, zc_flags, static_cast<int>(buf_index)};
    }

    [[nodiscard]] SpliceAwaiter splice(int fd_in, int64_t off_in, os_socket_t fd_out, int64_t off_out,
                                       unsigned int nbytes, unsigned int flags = 0) noexcept {
        return SpliceAwaiter{*this, fd_in, off_in, fd_out, off_out, nbytes, flags};
    }

    [[nodiscard]] ConnectAwaiter connect(os_socket_t fd, const sockaddr* addr, socklen_t addr_len) noexcept {
        return ConnectAwaiter{*this, fd, addr, addr_len};
    }

    [[nodiscard]] PollAwaiter poll(os_socket_t fd, unsigned poll_mask) noexcept {
        return PollAwaiter{*this, fd, poll_mask};
    }

    [[nodiscard]] TimeoutAwaiter timeout(uint64_t ns) noexcept {
        return TimeoutAwaiter{*this, ns};
    }

    [[nodiscard]] CloseAwaiter close(os_socket_t fd) noexcept {
        return CloseAwaiter{*this, fd};
    }

    [[nodiscard]] ShutdownAwaiter shutdown(os_socket_t fd, int how) noexcept {
        return ShutdownAwaiter{*this, fd, how};
    }

    [[nodiscard]] CancelAwaiter cancel(uint64_t user_data, int flags = 0) noexcept {
        return CancelAwaiter{*this, user_data, flags};
    }

    [[nodiscard]] RecvmsgAwaiter recvmsg(os_socket_t fd, msghdr* msg) noexcept {
        return RecvmsgAwaiter{*this, fd, msg};
    }

    // Engine-level send_all loop (reusable across all engines)
    [[nodiscard]] Task<int> send_all(os_socket_t fd, const void* buf, size_t len);
    [[nodiscard]] Task<int> send_all(os_socket_t fd, const char* str);
    [[nodiscard]] Task<int> send_all(os_socket_t fd, std::string_view data);
    [[nodiscard]] Task<int> send_all(os_socket_t fd, std::span<const uint8_t> data);

    // Factory
    static std::unique_ptr<IoEngine> create(const EngineConfig& config = {});
    static std::unique_ptr<IoEngine> create(const EngineConfig& config, BufferPool& buffer_pool);

    [[nodiscard]] uint64_t id() const noexcept { return engine_id_; }

    // Helper: resume continuation safely
    static void resume_continuation(IoAwaiter& aw) noexcept {
        if (aw.continuation && !aw.continuation.done()) {
            auto cont = aw.continuation;
            aw.continuation = nullptr;
            cont.resume();
        }
    }

protected:
    uint64_t engine_id_{next_engine_id()};

private:
    static uint64_t next_engine_id() noexcept {
        static std::atomic<uint64_t> s_id{1};
        return s_id.fetch_add(1, std::memory_order_relaxed);
    }
};

// Inline implementations of submit() delegating to engine
inline void AcceptAwaiter::submit() noexcept { engine.submit_accept(*this); }
inline void RecvProvidedAwaiter::submit() noexcept { engine.submit_recv_provided(*this); }
inline void RecvAwaiter::submit() noexcept { engine.submit_recv(*this); }
inline void SendAwaiter::submit() noexcept { engine.submit_send(*this); }
inline void SendZcAwaiter::submit() noexcept { engine.submit_send_zc(*this); }
inline void SpliceAwaiter::submit() noexcept { engine.submit_splice(*this); }
inline void ConnectAwaiter::submit() noexcept { engine.submit_connect(*this); }
inline void PollAwaiter::submit() noexcept { engine.submit_poll(*this); }
inline void TimeoutAwaiter::submit() noexcept { engine.submit_timeout(*this); }
inline void CloseAwaiter::submit() noexcept { engine.submit_close(*this); }
inline void ShutdownAwaiter::submit() noexcept { engine.submit_shutdown(*this); }
inline void CancelAwaiter::submit() noexcept { engine.submit_cancel(*this); }
inline void RecvmsgAwaiter::submit() noexcept { engine.submit_recvmsg(*this); }

inline void MultishotAcceptStream::StreamState::submit() noexcept { engine.submit_multishot_accept(*this); }
inline void MultishotRecvStream::StreamState::submit() noexcept { engine.submit_multishot_recv(*this); }

} // namespace aegon::core
