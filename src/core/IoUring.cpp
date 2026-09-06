#include "IoUring.h"
#include <unistd.h>
#include <cstring>

namespace aegon::core {

IoUring::IoUring(uint32_t entries, uint32_t flags) {
    struct io_uring_params params{};
    params.flags = flags;
    int ret = io_uring_queue_init_params(entries, &ring_, &params);
    if (ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init_params failed");
    }
    initialized_ = true;
}

IoUring::~IoUring() {
    if (initialized_) {
        io_uring_queue_exit(&ring_);
        initialized_ = false;
    }
}

IoUring::IoUring(IoUring&& other) noexcept
    : ring_(other.ring_), initialized_(other.initialized_) {
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

void IoUring::MultishotAcceptAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring.ring_);
    if (!sqe) [[unlikely]] {
        ring.submit_and_wait(0);
        sqe = io_uring_get_sqe(&ring.ring_);
    }
    io_uring_prep_accept(sqe, listen_fd, 
                         reinterpret_cast<struct sockaddr*>(&addr), 
                         &addr_len, 0);
    io_uring_sqe_set_data(sqe, this);
}

AcceptResult IoUring::MultishotAcceptAwaiter::await_resume() noexcept {
    AcceptResult res;
    res.fd = result;
    res.addr = addr;
    res.addr_len = addr_len;
    res.has_more = false;
    return res;
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

size_t IoUring::process_completions() noexcept {
    struct io_uring_cqe* cqe = nullptr;
    unsigned head = 0;
    size_t count = 0;

    io_uring_for_each_cqe(&ring_, head, cqe) {
        ++count;
        auto* awaiter = static_cast<IoAwaiter*>(io_uring_cqe_get_data(cqe));
        if (awaiter) {
            awaiter->result = cqe->res;
            awaiter->cqe_flags = cqe->flags;
            if (awaiter->continuation && !awaiter->continuation.done()) {
                awaiter->continuation.resume();
            }
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
