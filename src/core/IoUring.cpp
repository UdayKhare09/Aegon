#include "IoUring.h"
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace aegon::core {

void IoUring::init(const IoUringConfig& config) {
    struct io_uring_params params{};
    params.flags = config.flags;
    if (config.enable_sqpoll) {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = config.sq_thread_idle_ms;
        if (config.sq_thread_cpu >= 0) {
            params.flags |= IORING_SETUP_SQ_AFF;
            params.sq_thread_cpu = static_cast<uint32_t>(config.sq_thread_cpu);
        }
    }

    int ret = io_uring_queue_init_params(config.entries, &ring_, &params);
    if (ret == 0) {
        initialized_ = true;
        sqpoll_enabled_ = (params.flags & IORING_SETUP_SQPOLL) != 0;
        return;
    }

    // Graceful fallback for SQPOLL if unprivileged or memory locked limit reached
    if (config.enable_sqpoll && (ret == -EPERM || ret == -EACCES || ret == -ENOMEM)) {
        std::memset(&params, 0, sizeof(params));
        params.flags = config.flags;
        ret = io_uring_queue_init_params(config.entries, &ring_, &params);
        if (ret == 0) {
            initialized_ = true;
            sqpoll_enabled_ = false;
            return;
        }
    }

    throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init_params failed");
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
      initialized_(other.initialized_),
      sqpoll_enabled_(other.sqpoll_enabled_) {
    other.initialized_ = false;
    other.sqpoll_enabled_ = false;
}

IoUring& IoUring::operator=(IoUring&& other) noexcept {
    if (this != &other) {
        if (initialized_) {
            io_uring_queue_exit(&ring_);
        }
        ring_ = other.ring_;
        initialized_ = other.initialized_;
        sqpoll_enabled_ = other.sqpoll_enabled_;
        other.initialized_ = false;
        other.sqpoll_enabled_ = false;
    }
    return *this;
}

void IoUring::MultishotAcceptAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
    }
    io_uring_prep_multishot_accept(sqe, listen_fd, 
                                   reinterpret_cast<struct sockaddr*>(&addr), 
                                   &addr_len, 0);
    io_uring_sqe_set_data(sqe, this);
}

AcceptResult IoUring::MultishotAcceptAwaiter::await_resume() noexcept {
    AcceptResult res;
    res.fd = result;
    res.addr = addr;
    res.addr_len = addr_len;
    res.has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;
    return res;
}

void MultishotAcceptStream::StreamAwaiter::submit() noexcept {
    // If not yet armed in kernel, submit IORING_ACCEPT_MULTISHOT SQE
    if (!stream.armed_) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&stream.ring_.ring_);
        if (!sqe) [[unlikely]] {
            stream.ring_.submit_and_wait(0);
            sqe = io_uring_get_sqe(&stream.ring_.ring_);
        }
        io_uring_prep_multishot_accept(sqe, stream.listen_fd_, nullptr, nullptr, 0);
        io_uring_sqe_set_data(sqe, this);
        stream.armed_ = true;
    }
    // If already armed, no SQE needed! Kernel will generate CQE directly on incoming connection.
}

AcceptResult MultishotAcceptStream::StreamAwaiter::await_resume() noexcept {
    AcceptResult res;
    res.fd = result;
    res.has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;
    if (!res.has_more || result < 0) {
        stream.armed_ = false;
    }
    return res;
}

void MultishotAcceptStream::cancel() noexcept {
    if (armed_ && ring_.raw_ring()) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring_.raw_ring());
        if (sqe) {
            io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(&awaiter_), 0);
            io_uring_sqe_set_data(sqe, nullptr);
            (void)io_uring_submit(ring_.raw_ring());
        }
        armed_ = false;
    }
}

void IoUring::MultishotRecvAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
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

void IoUring::SendAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
    }
    io_uring_prep_send(sqe, socket_fd, buf, len, MSG_NOSIGNAL);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::SendAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::SendZcAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
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
            continuation.resume();
        }
        return;
    }

    if (flags & IORING_CQE_F_NOTIF) {
        result = bytes_sent;
        cqe_flags = flags;
        if (continuation && !continuation.done()) {
            continuation.resume();
        }
    } else {
        bytes_sent = res;
        if (!(flags & IORING_CQE_F_MORE)) {
            result = bytes_sent;
            cqe_flags = flags;
            if (continuation && !continuation.done()) {
                continuation.resume();
            }
        }
    }
}

int IoUring::SendZcAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::SpliceAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
    }
    io_uring_prep_splice(sqe, fd_in, off_in, fd_out, off_out, nbytes, splice_flags);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::SpliceAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::CloseAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
    }
    io_uring_prep_close(sqe, fd);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::CloseAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::CancelAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
    }
    io_uring_prep_cancel64(sqe, user_data, cancel_flags);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::CancelAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::RecvmsgAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
    }
    io_uring_prep_recvmsg(sqe, socket_fd, msg, 0);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::RecvmsgAwaiter::await_resume() noexcept {
    return result;
}

void IoUring::TimeoutAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
    }
    io_uring_prep_timeout(sqe, &ts, 0, 0);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::TimeoutAwaiter::await_resume() noexcept {
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

} // namespace aegon::core

