#include "IoUring.h"
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace aegon::core {

void IoUring::init(const IoUringConfig& config) {
    struct io_uring_params params{};
    // Modern Linux 6.0+ zero-contention thread-per-core io_uring features:
    // - IORING_SETUP_SINGLE_ISSUER: lockless submission queue for single-threaded loop
    // - IORING_SETUP_COOP_TASKRUN: run task_work cooperatively to eliminate inter-core IPIs
    // - IORING_SETUP_TASKRUN_FLAG: signal pending task_work via SQ ring flag
    // - IORING_SETUP_SUBMIT_ALL: submit entire batch unconditionally
    params.flags = config.flags | 
                   IORING_SETUP_SINGLE_ISSUER | 
                   IORING_SETUP_COOP_TASKRUN | 
                   IORING_SETUP_TASKRUN_FLAG | 
                   IORING_SETUP_SUBMIT_ALL;

    int ret = io_uring_queue_init_params(config.entries, &ring_, &params);
    if (ret != 0) {
        throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init_params failed");
    }
    initialized_ = true;
}

IoUring::IoUring(uint32_t entries, uint32_t flags) {
    IoUringConfig cfg;
    cfg.entries = entries;
    cfg.flags = flags;
    init(cfg);
}

IoUring::IoUring(const IoUringConfig& config) {
    init(config);
}

IoUring::~IoUring() {
    if (initialized_) {
        io_uring_queue_exit(&ring_);
        initialized_ = false;
    }
}

IoUring::IoUring(IoUring&& other) noexcept
    : ring_(other.ring_),
      initialized_(other.initialized_) {
    other.initialized_ = false;
}

IoUring& IoUring::operator=(IoUring&& other) noexcept {
    if (this != &other) {
        if (initialized_) {
            io_uring_queue_exit(&ring_);
        }
        ring_ = other.ring_;
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}

struct io_uring_sqe* IoUring::acquire_sqe() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) [[unlikely]] {
        int ret = io_uring_submit(&ring_);
        if (ret >= 0) {
            sqe = io_uring_get_sqe(&ring_);
        }
    }
    return sqe;
}

void IoUring::AcceptAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_accept(sqe, listen_fd, 
                         reinterpret_cast<struct sockaddr*>(&addr), 
                         &addr_len, 0);
    io_uring_sqe_set_data(sqe, this);
}

AcceptResult IoUring::AcceptAwaiter::await_resume() noexcept {
    AcceptResult res;
    res.fd = result;
    res.addr = addr;
    res.addr_len = addr_len;
    res.has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;
    return res;
}

MultishotAcceptStream::StreamState::~StreamState() {
    for (const auto& res : backlog) {
        if (res.fd >= 0) {
            ::close(res.fd);
        }
    }
    backlog.clear();
}

void MultishotAcceptStream::StreamState::submit() noexcept {
    if (!armed) {
        struct io_uring_sqe* sqe = ring.acquire_sqe();
        if (!sqe) [[unlikely]] {
            result = -EBUSY;
            if (continuation && !continuation.done()) {
                auto cont = continuation;
                continuation = nullptr;
                cont.resume();
            }
            return;
        }
        io_uring_prep_multishot_accept(sqe, listen_fd, nullptr, nullptr, 0);
        io_uring_sqe_set_data(sqe, this);
        armed = true;
        self_ = shared_from_this();
    }
}

void MultishotAcceptStream::StreamState::on_completion(int res, uint32_t flags) noexcept {
    AcceptResult ar;
    ar.fd = res;
    ar.has_more = (flags & IORING_CQE_F_MORE) != 0;
    if (!ar.has_more || res < 0) {
        armed = false;
        finished = true;
    }

    if (continuation && !continuation.done()) {
        current_result = ar;
        auto cont = continuation;
        continuation = nullptr;
        cont.resume();
    } else {
        if (ar.fd >= 0) {
            backlog.push_back(ar);
        }
    }

    if (!armed) {
        self_.reset();
    }
}

MultishotAcceptStream::MultishotAcceptStream(IoUring& ring, int listen_fd)
    : state_(std::make_shared<StreamState>(ring, listen_fd)) {}

MultishotAcceptStream::~MultishotAcceptStream() {
    cancel();
}

MultishotAcceptStream::MultishotAcceptStream(MultishotAcceptStream&&) noexcept = default;
MultishotAcceptStream& MultishotAcceptStream::operator=(MultishotAcceptStream&&) noexcept = default;

void MultishotAcceptStream::cancel() noexcept {
    if (state_ && state_->armed && state_->ring.raw_ring()) {
        struct io_uring_sqe* sqe = state_->ring.acquire_sqe();
        if (sqe) {
            io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(state_.get()), 0);
            io_uring_sqe_set_data(sqe, nullptr);
            (void)io_uring_submit(state_->ring.raw_ring());
        }
        state_->armed = false;
        state_->cancelled = true;
        state_->finished = true;
    }
}

MultishotRecvStream::StreamState::~StreamState() {
    backlog.clear();
}

void MultishotRecvStream::StreamState::submit() noexcept {
    if (!armed && !finished && !cancelled) {
        struct io_uring_sqe* sqe = ring.acquire_sqe();
        if (!sqe) [[unlikely]] {
            result = -EBUSY;
            finished = true;
            if (continuation && !continuation.done()) {
                auto cont = continuation;
                continuation = nullptr;
                cont.resume();
            }
            return;
        }
        io_uring_prep_recv_multishot(sqe, socket_fd, nullptr, 0, 0);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = bgid;
        io_uring_sqe_set_data(sqe, this);
        armed = true;
        self_ = shared_from_this();
    }
}

void MultishotRecvStream::StreamState::on_completion(int res, uint32_t flags) noexcept {
    RecvResult rr;
    rr.bytes = res;
    rr.has_more = (flags & IORING_CQE_F_MORE) != 0;
    if (flags & IORING_CQE_F_BUFFER) {
        rr.bid = static_cast<uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
    }
    if (!rr.has_more || res <= 0) {
        armed = false;
        finished = true;
    }

    if (continuation && !continuation.done()) {
        current_result = rr;
        auto cont = continuation;
        continuation = nullptr;
        cont.resume();
    } else {
        backlog.push_back(rr);
    }

    if (!armed) {
        self_.reset();
    }
}

MultishotRecvStream::MultishotRecvStream(IoUring& ring, int socket_fd, uint16_t bgid)
    : state_(std::make_shared<StreamState>(ring, socket_fd, bgid)) {}

MultishotRecvStream::~MultishotRecvStream() {
    cancel();
}

MultishotRecvStream::MultishotRecvStream(MultishotRecvStream&&) noexcept = default;
MultishotRecvStream& MultishotRecvStream::operator=(MultishotRecvStream&&) noexcept = default;

void MultishotRecvStream::cancel() noexcept {
    if (state_ && state_->armed && state_->ring.raw_ring()) {
        struct io_uring_sqe* sqe = state_->ring.acquire_sqe();
        if (sqe) {
            io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(state_.get()), 0);
            io_uring_sqe_set_data(sqe, nullptr);
            (void)io_uring_submit(state_->ring.raw_ring());
        }
        state_->armed = false;
        state_->cancelled = true;
        state_->finished = true;
    }
}

void IoUring::MultishotRecvAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_recv(sqe, socket_fd, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = bgid;
    io_uring_sqe_set_data(sqe, this);
}

RecvResult IoUring::MultishotRecvAwaiter::await_resume() noexcept {
    RecvResult res;
    res.bytes = result;
    if (cqe_flags & IORING_CQE_F_BUFFER) {
        res.bid = static_cast<uint16_t>(cqe_flags >> IORING_CQE_BUFFER_SHIFT);
    }
    res.has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;
    return res;
}

void IoUring::RecvAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_recv(sqe, socket_fd, buf, len, flags);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::RecvAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::ConnectAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_connect(sqe, socket_fd, 
                          reinterpret_cast<const struct sockaddr*>(&addr), 
                          addr_len);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::ConnectAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::SendAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_send(sqe, socket_fd, buf, len, MSG_NOSIGNAL);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::SendAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::SendZcAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    if (fixed_buf_idx >= 0) {
        io_uring_prep_send_zc_fixed(sqe, socket_fd, buf, len, MSG_NOSIGNAL, 
                                    static_cast<unsigned>(zc_flags), 
                                    static_cast<unsigned>(fixed_buf_idx));
    } else {
        io_uring_prep_send_zc(sqe, socket_fd, buf, len, MSG_NOSIGNAL, 
                              static_cast<unsigned>(zc_flags));
    }
    io_uring_sqe_set_data(sqe, this);
}

void IoUring::SendZcAwaiter::on_completion(int res, uint32_t flags) noexcept {
    if (res < 0) {
        result = res;
        cqe_flags = flags;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }

    if (flags & IORING_CQE_F_NOTIF) {
        notif_received = true;
    } else {
        bytes_sent = res;
        send_completed = true;
        if (!(flags & IORING_CQE_F_MORE)) {
            notif_received = true;
        }
    }

    if (send_completed && notif_received) {
        result = bytes_sent;
        cqe_flags = flags;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
    }
}

int IoUring::SendZcAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::SpliceAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_splice(sqe, fd_in, off_in, fd_out, off_out, nbytes, splice_flags);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::SpliceAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::CloseAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_close(sqe, fd);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::CloseAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::CancelAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_cancel64(sqe, user_data, cancel_flags);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::CancelAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::RecvmsgAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_recvmsg(sqe, socket_fd, msg, 0);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::RecvmsgAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::TimeoutAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_timeout(sqe, &ts, 0, 0);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::TimeoutAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::PollAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        if (continuation && !continuation.done()) {
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        }
        return;
    }
    io_uring_prep_poll_add(sqe, fd, poll_mask);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::PollAwaiter::await_resume() noexcept {
    return result;
}

size_t IoUring::process_completions() noexcept {
    struct io_uring_cqe* cqe = nullptr;
    unsigned head = 0;
    size_t count = 0;

    io_uring_for_each_cqe(&ring_, head, cqe) {
        ++count;
        auto* awaiter = static_cast<IoAwaiter*>(io_uring_cqe_get_data(cqe));
        if (awaiter) {
            awaiter->on_completion(cqe->res, cqe->flags);
        }
    }
    io_uring_cq_advance(&ring_, count);
    return count;
}

int IoUring::submit_and_wait(uint32_t min_complete) {
    int ret = io_uring_submit_and_wait(&ring_, min_complete);
    if (ret < 0 && ret != -EINTR) {
        throw std::system_error(-ret, std::generic_category(), "io_uring_submit_and_wait failed");
    }
    return ret;
}

Task<int> IoUring::send_all(int fd, const void* buf, size_t len) {
    if (len == 0) co_return 0;
    size_t total_sent = 0;
    const uint8_t* ptr = static_cast<const uint8_t*>(buf);
    while (total_sent < len) {
        int n = co_await send(fd, std::span<const uint8_t>(ptr + total_sent, len - total_sent));
        if (n <= 0) {
            co_return (total_sent > 0) ? static_cast<int>(total_sent) : n;
        }
        total_sent += static_cast<size_t>(n);
    }
    co_return static_cast<int>(total_sent);
}

Task<int> IoUring::send_all(int fd, const char* str) {
    if (!str) co_return 0;
    co_return co_await send_all(fd, std::string_view(str));
}

Task<int> IoUring::send_all(int fd, std::string data) {
    co_return co_await send_all(fd, data.data(), data.size());
}

Task<int> IoUring::send_all(int fd, std::string_view data) {
    co_return co_await send_all(fd, data.data(), data.size());
}

Task<int> IoUring::send_all(int fd, std::span<const uint8_t> data) {
    co_return co_await send_all(fd, data.data(), data.size());
}

} // namespace aegon::core

