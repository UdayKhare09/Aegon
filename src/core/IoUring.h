#pragma once

#include <liburing.h>
#include <coroutine>
#include <cstdint>
#include <span>
#include "Task.h"
#include <stdexcept>
#include <system_error>
#include <sys/socket.h>
#include <netinet/in.h>
#include <memory>
#include <vector>
#include <cstring>

namespace aegon::core {

class IoUring;

/**
 * @brief Base class for io_uring asynchronous operations.
 * Associated with a coroutine handle and stored in SQE user_data.
 */
struct IoAwaiter {
    std::coroutine_handle<> continuation{nullptr};
    int32_t result{0};
    uint32_t cqe_flags{0};

    virtual ~IoAwaiter() = default;

    [[nodiscard]] bool await_ready() const noexcept { return false; }

    virtual void submit() noexcept = 0;

    inline void await_suspend(std::coroutine_handle<> h) noexcept {
        continuation = h;
        submit();
    }

    virtual void on_completion(int res, uint32_t flags) noexcept {
        result = res;
        cqe_flags = flags;
        if (continuation && !continuation.done()) {
            continuation.resume();
        }
    }
};

/**
 * @brief Configuration settings for modern io_uring initialization.
 */
struct IoUringConfig {
    uint32_t entries{4096};
    uint32_t flags{0};
};

/**
 * @brief Represents an accepted connection from a multishot accept.
 */
struct AcceptResult {
    int fd{-1};
    sockaddr_storage addr{};
    socklen_t addr_len{sizeof(sockaddr_storage)};
    bool has_more{false};
};

/**
 * @brief Represents received data from a multishot recv into a provided buffer ring.
 */
struct RecvResult {
    int bytes{0};
    uint16_t bid{0};
    bool has_more{false};
};

class MultishotAcceptStream;
class MultishotRecvStream;

/**
 * @brief Modern C++26 high-performance io_uring engine.
 *
 * Implements multishot accept, multishot recv (using PBUF_RING), zero-copy send,
 * zero-copy splice, and zero-allocation coroutine resumption.
 */
class IoUring {
public:
    static constexpr uint32_t DEFAULT_RING_ENTRIES = 4096;

    explicit IoUring(uint32_t entries = DEFAULT_RING_ENTRIES, uint32_t flags = 0);
    explicit IoUring(const IoUringConfig& config);
    ~IoUring();

    IoUring(const IoUring&) = delete;
    IoUring& operator=(const IoUring&) = delete;
    IoUring(IoUring&&) noexcept;
    IoUring& operator=(IoUring&&) noexcept;

    // --- Coroutine Awaiters ---

    // One-Shot Accept Awaiter
    struct AcceptAwaiter : IoAwaiter {
        IoUring& ring;
        int listen_fd;
        sockaddr_storage addr{};
        socklen_t addr_len{sizeof(sockaddr_storage)};

        AcceptAwaiter(IoUring& r, int fd) noexcept : ring(r), listen_fd(fd) {}

        void submit() noexcept override;
        [[nodiscard]] AcceptResult await_resume() noexcept;
    };
    using MultishotAcceptAwaiter = AcceptAwaiter;

    // One-Shot Recv with Provided Buffer Ring (PBUF_RING)
    struct RecvProvidedAwaiter : IoAwaiter {
        IoUring& ring;
        int socket_fd;
        uint16_t bgid;

        RecvProvidedAwaiter(IoUring& r, int fd, uint16_t b) noexcept 
            : ring(r), socket_fd(fd), bgid(b) {}

        void submit() noexcept override;
        [[nodiscard]] RecvResult await_resume() noexcept;
    };
    using MultishotRecvAwaiter = RecvProvidedAwaiter;

    // Direct Recv
    struct RecvAwaiter : IoAwaiter {
        IoUring& ring;
        int socket_fd;
        void* buf;
        size_t len;
        int flags{0};

        RecvAwaiter(IoUring& r, int fd, void* b, size_t l, int fl = 0) noexcept
            : ring(r), socket_fd(fd), buf(b), len(l), flags(fl) {}

        void submit() noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Connect
    struct ConnectAwaiter : IoAwaiter {
        IoUring& ring;
        int socket_fd;
        sockaddr_storage addr{};
        socklen_t addr_len{0};

        ConnectAwaiter(IoUring& r, int fd, const sockaddr* a, socklen_t l) noexcept
            : ring(r), socket_fd(fd), addr_len(std::min<socklen_t>(l, sizeof(sockaddr_storage))) {
            if (a && addr_len > 0) {
                std::memcpy(&addr, a, addr_len);
            }
        }

        void submit() noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Standard Send
    struct SendAwaiter : IoAwaiter {
        IoUring& ring;
        int socket_fd;
        const void* buf;
        size_t len;

        SendAwaiter(IoUring& r, int fd, const void* b, size_t l) noexcept 
            : ring(r), socket_fd(fd), buf(b), len(l) {}

        void submit() noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Zero-Copy Send: uses io_uring_prep_send_zc and send_zc_fixed
    struct SendZcAwaiter : IoAwaiter {
        IoUring& ring;
        int socket_fd;
        const void* buf;
        size_t len;
        int zc_flags{0};
        int fixed_buf_idx{-1};
        int bytes_sent{0};
        bool send_completed{false};
        bool notif_received{false};

        SendZcAwaiter(IoUring& r, int fd, const void* b, size_t l, int zc_fl = 0, int buf_idx = -1) noexcept 
            : ring(r), socket_fd(fd), buf(b), len(l), zc_flags(zc_fl), fixed_buf_idx(buf_idx) {}

        void submit() noexcept override;
        void on_completion(int res, uint32_t flags) noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Zero-Copy Splice: splices data directly between file and socket descriptors via kernel pipe
    struct SpliceAwaiter : IoAwaiter {
        IoUring& ring;
        int fd_in;
        int64_t off_in;
        int fd_out;
        int64_t off_out;
        unsigned int nbytes;
        unsigned int splice_flags;

        SpliceAwaiter(IoUring& r, int in, int64_t o_in, int out, int64_t o_out, unsigned int bytes, unsigned int flags = 0) noexcept
            : ring(r), fd_in(in), off_in(o_in), fd_out(out), off_out(o_out), nbytes(bytes), splice_flags(flags) {}

        void submit() noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Async Close
    struct CloseAwaiter : IoAwaiter {
        IoUring& ring;
        int fd;

        CloseAwaiter(IoUring& r, int f) noexcept : ring(r), fd(f) {}

        void submit() noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Async Cancel
    struct CancelAwaiter : IoAwaiter {
        IoUring& ring;
        uint64_t user_data;
        int cancel_flags;

        CancelAwaiter(IoUring& r, uint64_t udata, int flags = 0) noexcept
            : ring(r), user_data(udata), cancel_flags(flags) {}

        void submit() noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Async Recvmsg (for UDP datagrams)
    struct RecvmsgAwaiter : IoAwaiter {
        IoUring& ring;
        int socket_fd;
        msghdr* msg;

        RecvmsgAwaiter(IoUring& r, int fd, msghdr* m) noexcept
            : ring(r), socket_fd(fd), msg(m) {}

        void submit() noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Async Timeout (for timers and loss recovery)
    struct TimeoutAwaiter : IoAwaiter {
        IoUring& ring;
        __kernel_timespec ts{};

        TimeoutAwaiter(IoUring& r, uint64_t ns) noexcept : ring(r) {
            ts.tv_sec = static_cast<int64_t>(ns / 1'000'000'000ULL);
            ts.tv_nsec = static_cast<int64_t>(ns % 1'000'000'000ULL);
        }

        void submit() noexcept override;
        int await_resume() noexcept;
    };

    // Async Poll (for non-blocking socket and pipe polling)
    struct PollAwaiter : IoAwaiter {
        IoUring& ring;
        int fd;
        unsigned poll_mask;

        PollAwaiter(IoUring& r, int f, unsigned mask) noexcept
            : ring(r), fd(f), poll_mask(mask) {}

        void submit() noexcept override;
        [[nodiscard]] int await_resume() noexcept;
    };

    // Helper builders
    [[nodiscard]] PollAwaiter poll(int fd, unsigned poll_mask) noexcept {
        return PollAwaiter{*this, fd, poll_mask};
    }

    [[nodiscard]] MultishotAcceptAwaiter accept(int listen_fd) noexcept {
        return MultishotAcceptAwaiter{*this, listen_fd};
    }

    [[nodiscard]] MultishotAcceptStream accept_multishot(int listen_fd) noexcept;

    [[nodiscard]] MultishotRecvStream recv_multishot_stream(int fd, uint16_t bgid) noexcept;
    [[nodiscard]] MultishotRecvStream recv_stream(int fd, uint16_t bgid) noexcept;

    [[nodiscard]] RecvProvidedAwaiter recv_provided(int fd, uint16_t bgid) noexcept {
        return RecvProvidedAwaiter{*this, fd, bgid};
    }

    [[nodiscard]] RecvProvidedAwaiter recv_multishot(int fd, uint16_t bgid) noexcept {
        return RecvProvidedAwaiter{*this, fd, bgid};
    }

    [[nodiscard]] RecvAwaiter recv(int fd, void* buf, size_t len, int flags = 0) noexcept {
        return RecvAwaiter{*this, fd, buf, len, flags};
    }

    [[nodiscard]] ConnectAwaiter connect(int fd, const sockaddr* addr, socklen_t addr_len) noexcept {
        return ConnectAwaiter{*this, fd, addr, addr_len};
    }

    [[nodiscard]] SendAwaiter send(int fd, std::span<const uint8_t> data) noexcept {
        return SendAwaiter{*this, fd, data.data(), data.size()};
    }

    [[nodiscard]] SendAwaiter send(int fd, std::string_view data) noexcept {
        return SendAwaiter{*this, fd, data.data(), data.size()};
    }

    [[nodiscard]] Task<int> send_all(int fd, const void* buf, size_t len);
    [[nodiscard]] Task<int> send_all(int fd, const char* str);
    [[nodiscard]] Task<int> send_all(int fd, std::string data);
    [[nodiscard]] Task<int> send_all(int fd, std::string_view data);
    [[nodiscard]] Task<int> send_all(int fd, std::span<const uint8_t> data);

    [[nodiscard]] SendZcAwaiter send_zc(int fd, std::span<const uint8_t> data, int zc_flags = 0) noexcept {
        return SendZcAwaiter{*this, fd, data.data(), data.size(), zc_flags, -1};
    }

    [[nodiscard]] SendZcAwaiter send_zc(int fd, std::string_view data, int zc_flags = 0) noexcept {
        return SendZcAwaiter{*this, fd, data.data(), data.size(), zc_flags, -1};
    }

    [[nodiscard]] SendZcAwaiter send_zc_fixed(int fd, const void* buf, size_t len, unsigned buf_index, int zc_flags = 0) noexcept {
        return SendZcAwaiter{*this, fd, buf, len, zc_flags, static_cast<int>(buf_index)};
    }

    [[nodiscard]] SpliceAwaiter splice(int fd_in, int64_t off_in, int fd_out, int64_t off_out, unsigned int nbytes, unsigned int flags = 0) noexcept {
        return SpliceAwaiter{*this, fd_in, off_in, fd_out, off_out, nbytes, flags};
    }

    [[nodiscard]] CloseAwaiter close(int fd) noexcept {
        return CloseAwaiter{*this, fd};
    }

    [[nodiscard]] CancelAwaiter cancel(uint64_t user_data, int flags = 0) noexcept {
        return CancelAwaiter{*this, user_data, flags};
    }

    [[nodiscard]] RecvmsgAwaiter recvmsg(int fd, msghdr* msg) noexcept {
        return RecvmsgAwaiter{*this, fd, msg};
    }

    [[nodiscard]] TimeoutAwaiter timeout(uint64_t ns) noexcept {
        return TimeoutAwaiter{*this, ns};
    }

    // Process all pending completion queue events (CQEs) and resume awaiting coroutines
    size_t process_completions() noexcept;

    // Submit pending submission queue entries (SQEs) and wait for at least min_complete
    int submit_and_wait(uint32_t min_complete = 1);

    // Direct access to underlying struct io_uring
    [[nodiscard]] struct io_uring* raw_ring() noexcept { return &ring_; }

    // Safely acquire an SQE, submitting pending requests if queue is full. Non-throwing.
    [[nodiscard]] struct io_uring_sqe* acquire_sqe() noexcept;

private:
    friend class MultishotAcceptStream;
    friend class MultishotRecvStream;
    void init(const IoUringConfig& config);

    struct io_uring ring_{};
    bool initialized_{false};
};

/**
 * @brief Asynchronous stream for IORING_ACCEPT_MULTISHOT.
 *
 * Keeps the accept request continually armed in the Linux kernel without
 * re-submitting SQEs per connection, eliminating submission queue traffic.
 */
class MultishotAcceptStream {
public:
    struct StreamState : IoAwaiter, std::enable_shared_from_this<StreamState> {
        IoUring& ring;
        int listen_fd{-1};
        bool armed{false};
        bool finished{false};
        bool cancelled{false};
        std::vector<AcceptResult> backlog{};
        AcceptResult current_result{};
        std::shared_ptr<StreamState> self_{nullptr};

        StreamState(IoUring& r, int fd) noexcept : ring(r), listen_fd(fd) {}
        ~StreamState();

        void submit() noexcept override;
        void on_completion(int res, uint32_t flags) noexcept override;
    };

    MultishotAcceptStream(IoUring& ring, int listen_fd);
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
                state.backlog.erase(state.backlog.begin());
                return res;
            }
            return state.current_result;
        }
    };

    [[nodiscard]] NextAwaiter next() noexcept {
        return NextAwaiter{*state_};
    }

    void cancel() noexcept;

    [[nodiscard]] bool is_armed() const noexcept { return state_ && state_->armed; }
    [[nodiscard]] int listen_fd() const noexcept { return state_ ? state_->listen_fd : -1; }

private:
    std::shared_ptr<StreamState> state_;
};

inline MultishotAcceptStream IoUring::accept_multishot(int listen_fd) noexcept {
    return MultishotAcceptStream{*this, listen_fd};
}

/**
 * @brief Asynchronous stream for IORING_RECV_MULTISHOT with provided buffer pools (PBUF_RING).
 *
 * Continuously yields packets from the socket into kernel provided buffers without
 * SQE re-submission on each read packet.
 */
class MultishotRecvStream {
public:
    struct StreamState : IoAwaiter, std::enable_shared_from_this<StreamState> {
        IoUring& ring;
        int socket_fd{-1};
        uint16_t bgid{0};
        bool armed{false};
        bool finished{false};
        bool cancelled{false};
        std::vector<RecvResult> backlog{};
        RecvResult current_result{};
        std::shared_ptr<StreamState> self_{nullptr};

        StreamState(IoUring& r, int fd, uint16_t b) noexcept 
            : ring(r), socket_fd(fd), bgid(b) {}
        ~StreamState();

        void submit() noexcept override;
        void on_completion(int res, uint32_t flags) noexcept override;
    };

    MultishotRecvStream(IoUring& ring, int socket_fd, uint16_t bgid);
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
                state.backlog.erase(state.backlog.begin());
                return res;
            }
            return state.current_result;
        }
    };

    [[nodiscard]] NextAwaiter next() noexcept {
        return NextAwaiter{*state_};
    }

    void cancel() noexcept;

    [[nodiscard]] bool is_armed() const noexcept { return state_ && state_->armed; }
    [[nodiscard]] int socket_fd() const noexcept { return state_ ? state_->socket_fd : -1; }

private:
    std::shared_ptr<StreamState> state_;
};

inline MultishotRecvStream IoUring::recv_multishot_stream(int fd, uint16_t bgid) noexcept {
    return MultishotRecvStream{*this, fd, bgid};
}

inline MultishotRecvStream IoUring::recv_stream(int fd, uint16_t bgid) noexcept {
    return MultishotRecvStream{*this, fd, bgid};
}

} // namespace aegon::core
