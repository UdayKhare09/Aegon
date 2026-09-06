#pragma once

#include <liburing.h>
#include <coroutine>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <system_error>
#include <sys/socket.h>
#include <netinet/in.h>

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

/**
 * @brief Modern C++26 high-performance io_uring engine.
 *
 * Implements multishot accept, multishot recv (using PBUF_RING), zero-copy send,
 * and zero-allocation coroutine resumption via SQE user_data pointers.
 */
class IoUring {
public:
    static constexpr uint32_t DEFAULT_RING_ENTRIES = 4096;

    explicit IoUring(uint32_t entries = DEFAULT_RING_ENTRIES, uint32_t flags = 0);
    ~IoUring();

    IoUring(const IoUring&) = delete;
    IoUring& operator=(const IoUring&) = delete;
    IoUring(IoUring&&) noexcept;
    IoUring& operator=(IoUring&&) noexcept;

    // --- Coroutine Awaiters ---

    // Multishot Accept: continually yields new client connections
    struct MultishotAcceptAwaiter : IoAwaiter {
        IoUring& ring;
        int listen_fd;
        sockaddr_storage addr{};
        socklen_t addr_len{sizeof(sockaddr_storage)};

        MultishotAcceptAwaiter(IoUring& r, int fd) noexcept : ring(r), listen_fd(fd) {}

        void submit() noexcept override;
        [[nodiscard]] AcceptResult await_resume() noexcept;
    };

    // Multishot Recv: continuously yields packets directly into a Provided Buffer Ring
    struct MultishotRecvAwaiter : IoAwaiter {
        IoUring& ring;
        int socket_fd;
        uint16_t bgid;

        MultishotRecvAwaiter(IoUring& r, int fd, uint16_t b) noexcept 
            : ring(r), socket_fd(fd), bgid(b) {}

        void submit() noexcept override;
        [[nodiscard]] RecvResult await_resume() noexcept;
    };

    // Zero-Copy Send: sends a memory buffer over a socket
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

    // Async Close
    struct CloseAwaiter : IoAwaiter {
        IoUring& ring;
        int fd;

        CloseAwaiter(IoUring& r, int f) noexcept : ring(r), fd(f) {}

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

    // Helper builders
    [[nodiscard]] MultishotAcceptAwaiter accept(int listen_fd) noexcept {
        return MultishotAcceptAwaiter{*this, listen_fd};
    }

    [[nodiscard]] MultishotRecvAwaiter recv_multishot(int fd, uint16_t bgid) noexcept {
        return MultishotRecvAwaiter{*this, fd, bgid};
    }

    [[nodiscard]] SendAwaiter send(int fd, std::span<const uint8_t> data) noexcept {
        return SendAwaiter{*this, fd, data.data(), data.size()};
    }

    [[nodiscard]] SendAwaiter send(int fd, std::string_view data) noexcept {
        return SendAwaiter{*this, fd, data.data(), data.size()};
    }

    [[nodiscard]] CloseAwaiter close(int fd) noexcept {
        return CloseAwaiter{*this, fd};
    }

    [[nodiscard]] RecvmsgAwaiter recvmsg(int fd, msghdr* msg) noexcept {
        return RecvmsgAwaiter{*this, fd, msg};
    }

    // Process all pending completion queue events (CQEs) and resume awaiting coroutines
    size_t process_completions() noexcept;

    // Submit pending submission queue entries (SQEs) and wait for at least min_complete
    int submit_and_wait(uint32_t min_complete = 1);

    // Direct access to underlying struct io_uring
    [[nodiscard]] struct io_uring* raw_ring() noexcept { return &ring_; }

private:
    struct io_uring ring_{};
    bool initialized_{false};
};

} // namespace aegon::core
