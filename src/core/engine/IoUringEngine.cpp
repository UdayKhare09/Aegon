#include "core/engine/IoUringEngine.h"
#include "core/BufferPool.h"
#include "log/Logger.h"
#include <unistd.h>
#include <cstring>
#include <system_error>
#include <sys/eventfd.h>
#include <poll.h>

namespace aegon::core {

void IoUringEngine::init(const EngineConfig& config) {
    struct io_uring_params params{};

    params.flags = config.flags |
                   IORING_SETUP_SINGLE_ISSUER |
#ifdef IORING_SETUP_DEFER_TASKRUN
                   IORING_SETUP_DEFER_TASKRUN |
#else
                   IORING_SETUP_COOP_TASKRUN |
                   IORING_SETUP_TASKRUN_FLAG |
#endif
                   IORING_SETUP_SUBMIT_ALL;

    uint32_t entries = config.entries > 0 ? config.entries : 4096;
    int ret = io_uring_queue_init_params(entries, &ring_, &params);
    if (ret != 0) {
        log::error("[IoUringEngine] io_uring_queue_init_params failed (entries={}, ret={}): {}",
                   entries, ret, std::strerror(-ret));
        throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init_params failed");
    }
    initialized_ = true;
    log::debug("[IoUringEngine] Initialized ring fd={}, entries={}, flags={:#x}",
               ring_.ring_fd, entries, params.flags);

    (void)io_uring_ring_dontfork(&ring_);
    (void)io_uring_register_ring_fd(&ring_);

    if (config.direct_fd_count > 0) {
        register_direct_fds(config.direct_fd_count);
    }
}

IoUringEngine::IoUringEngine(const EngineConfig& config, BufferPool* pool) {
    init(config);
    if (pool) {
        pool->register_with_io_uring(&ring_);
    }
    wakeup_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wakeup_fd_ >= 0) {
        arm_wakeup();
    }
}

void IoUringEngine::WakeupAwaiter::submit() noexcept {
    if (engine.wakeup_fd_ < 0) return;
    struct io_uring_sqe* sqe = engine.acquire_sqe();
    if (!sqe) return;
    io_uring_prep_poll_add(sqe, engine.wakeup_fd_, POLLIN);
    io_uring_sqe_set_data(sqe, this);
}

void IoUringEngine::WakeupAwaiter::on_completion(int res, uint32_t /*flags*/) noexcept {
    if (res > 0 && engine.wakeup_fd_ >= 0) {
        uint64_t v = 0;
        (void)::read(engine.wakeup_fd_, &v, sizeof(v));
    }
    if (engine.wakeup_fd_ >= 0) {
        submit();
    }
}

void IoUringEngine::arm_wakeup() noexcept {
    wakeup_awaiter_.submit();
}

void IoUringEngine::register_buffer_pool(BufferPool& pool, bool register_buffers) {
    pool.register_with_io_uring(&ring_, register_buffers);
}

IoUringEngine::~IoUringEngine() {
    int wfd = wakeup_fd_;
    wakeup_fd_ = -1;
    if (wfd >= 0) {
        ::close(wfd);
    }
    if (initialized_) {
        if (files_registered_) {
            io_uring_unregister_files(&ring_);
            files_registered_ = false;
        }
        io_uring_queue_exit(&ring_);
        initialized_ = false;
    }
}

IoUringEngine::IoUringEngine(IoUringEngine&& other) noexcept
    : ring_(other.ring_),
      initialized_(other.initialized_),
      files_registered_(other.files_registered_),
      direct_fd_slots_(other.direct_fd_slots_),
      wakeup_fd_(other.wakeup_fd_) {
    other.initialized_      = false;
    other.files_registered_ = false;
    other.direct_fd_slots_  = 0;
    other.wakeup_fd_        = -1;
    std::memset(&other.ring_, 0, sizeof(other.ring_));
}

IoUringEngine& IoUringEngine::operator=(IoUringEngine&& other) noexcept {
    if (this != &other) {
        if (wakeup_fd_ >= 0) {
            ::close(wakeup_fd_);
        }
        if (initialized_) {
            if (files_registered_) {
                io_uring_unregister_files(&ring_);
            }
            io_uring_queue_exit(&ring_);
        }

        ring_             = other.ring_;
        initialized_      = other.initialized_;
        files_registered_ = other.files_registered_;
        direct_fd_slots_  = other.direct_fd_slots_;
        wakeup_fd_        = other.wakeup_fd_;

        other.initialized_      = false;
        other.files_registered_ = false;
        other.direct_fd_slots_  = 0;
        other.wakeup_fd_        = -1;
        std::memset(&other.ring_, 0, sizeof(other.ring_));
    }
    return *this;
}

void IoUringEngine::wakeup() noexcept {
    if (wakeup_fd_ >= 0) {
        uint64_t one = 1;
        (void)::write(wakeup_fd_, &one, sizeof(one));
    }
}

struct io_uring_sqe* IoUringEngine::acquire_sqe() noexcept {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) [[unlikely]] {
        (void)io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
    }
    return sqe;
}

int IoUringEngine::submit_and_wait(uint32_t min_complete) {
    if (!initialized_) return -1;
    int ret = io_uring_submit_and_wait(&ring_, min_complete);
    if (ret < 0 && ret != -EINTR && ret != -EBUSY) [[unlikely]] {
        log::error("[IoUringEngine] io_uring_submit_and_wait failed: {}", std::strerror(-ret));
    }
    return ret;
}

size_t IoUringEngine::process_completions() noexcept {
    if (!initialized_) return 0;

    struct io_uring_cqe* cqe = nullptr;
    unsigned head = 0;
    size_t count = 0;

    io_uring_for_each_cqe(&ring_, head, cqe) {
        ++count;
        uint64_t user_data = cqe->user_data;
        int res            = cqe->res;
        uint32_t flags     = cqe->flags;

        if (user_data != 0) {
            if (!cancelled_user_data_.empty()) {
                auto it = cancelled_user_data_.find(user_data);
                if (it != cancelled_user_data_.end()) {
                    if ((flags & 0x02 /* IORING_CQE_F_MORE */) == 0 || res < 0) {
                        cancelled_user_data_.erase(it);
                    }
                    continue;
                }
            }
            auto* awaiter = reinterpret_cast<IoAwaiter*>(user_data);
            awaiter->on_completion(res, flags);
        }
    }

    if (count > 0) {
        io_uring_cq_advance(&ring_, static_cast<unsigned>(count));
    }
    return count;
}

// ---------------------------------------------------------------------------
// Awaiter Submissions
// ---------------------------------------------------------------------------

void IoUringEngine::submit_accept(AcceptAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_accept(sqe, aw.listen_fd, reinterpret_cast<sockaddr*>(&aw.addr), &aw.addr_len, SOCK_CLOEXEC);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        st.result = -EBUSY;
        resume_continuation(st);
        return;
    }
    if (st.use_direct) {
        io_uring_prep_multishot_accept_direct(sqe, st.listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
    } else {
        io_uring_prep_multishot_accept(sqe, st.listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
    }
    io_uring_sqe_set_data(sqe, &st);
    st.armed = true;
}

void IoUringEngine::cancel_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept {
    if (st.armed && initialized_) {
        cancelled_user_data_.insert(reinterpret_cast<uint64_t>(&st));
        struct io_uring_sqe* sqe = acquire_sqe();
        if (sqe) {
            io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(&st), 0);
            io_uring_sqe_set_data(sqe, nullptr);
            (void)io_uring_submit(&ring_);
        }
        st.armed = false;
    }
}

void IoUringEngine::submit_recv_provided(RecvProvidedAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_recv(sqe, aw.socket_fd, nullptr, 0, 0);
    sqe->flags    |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = aw.bgid;
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_multishot_recv(MultishotRecvStream::StreamState& st) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        st.result = -EBUSY;
        resume_continuation(st);
        return;
    }
    io_uring_prep_recv_multishot(sqe, st.socket_fd, nullptr, 0, 0);
    sqe->flags    |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = st.bgid;
    io_uring_sqe_set_data(sqe, &st);
    st.armed = true;
}

void IoUringEngine::cancel_multishot_recv(MultishotRecvStream::StreamState& st) noexcept {
    if (st.armed && initialized_) {
        cancelled_user_data_.insert(reinterpret_cast<uint64_t>(&st));
        struct io_uring_sqe* sqe = acquire_sqe();
        if (sqe) {
            io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(&st), 0);
            io_uring_sqe_set_data(sqe, nullptr);
            (void)io_uring_submit(&ring_);
        }
        st.armed = false;
    }
}

void IoUringEngine::submit_recv(RecvAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_recv(sqe, aw.socket_fd, aw.buf, aw.len, aw.flags_arg);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_send(SendAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_send(sqe, aw.socket_fd, aw.buf, aw.len, MSG_NOSIGNAL);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_send_zc(SendZcAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    if (aw.fixed_buf_idx >= 0) {
        io_uring_prep_send_zc_fixed(sqe, aw.socket_fd, aw.buf, aw.len, MSG_NOSIGNAL,
                                    static_cast<unsigned>(aw.zc_flags),
                                    static_cast<unsigned>(aw.fixed_buf_idx));
    } else {
        io_uring_prep_send_zc(sqe, aw.socket_fd, aw.buf, aw.len, MSG_NOSIGNAL,
                              static_cast<unsigned>(aw.zc_flags));
    }
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_splice(SpliceAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_splice(sqe, aw.fd_in, aw.off_in, aw.fd_out, aw.off_out, aw.nbytes, aw.splice_flags);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_connect(ConnectAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_connect(sqe, aw.socket_fd, reinterpret_cast<const sockaddr*>(&aw.addr), aw.addr_len);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_poll(PollAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_poll_add(sqe, aw.fd, aw.poll_mask);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_timeout(TimeoutAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_timeout(sqe, reinterpret_cast<__kernel_timespec*>(&aw.sec), 0, 0);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_close(CloseAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_close(sqe, aw.fd);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_shutdown(ShutdownAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_shutdown(sqe, aw.fd, aw.how);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_cancel(CancelAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    cancelled_user_data_.insert(aw.user_data);
    io_uring_prep_cancel64(sqe, aw.user_data, aw.cancel_flags);
    io_uring_sqe_set_data(sqe, &aw);
}

void IoUringEngine::submit_recvmsg(RecvmsgAwaiter& aw) noexcept {
    struct io_uring_sqe* sqe = acquire_sqe();
    if (!sqe) [[unlikely]] {
        aw.result = -EBUSY;
        resume_continuation(aw);
        return;
    }
    io_uring_prep_recvmsg(sqe, aw.socket_fd, aw.msg, 0);
    io_uring_sqe_set_data(sqe, &aw);
}

// ---------------------------------------------------------------------------
// Direct file descriptor registration
// ---------------------------------------------------------------------------

bool IoUringEngine::register_direct_fds(unsigned count) noexcept {
    if (!initialized_ || files_registered_ || count == 0) return false;
    std::vector<int> files(count, -1);
    int ret = io_uring_register_files(&ring_, files.data(), count);
    if (ret != 0) {
        log::warn("[IoUringEngine] io_uring_register_files (count={}) failed: {}", count, std::strerror(-ret));
        return false;
    }
    files_registered_ = true;
    direct_fd_slots_  = count;
    log::debug("[IoUringEngine] Registered direct file table with {} slots", count);
    return true;
}

void IoUringEngine::update_direct_fd(unsigned slot, int fd) {
    if (!files_registered_ || slot >= direct_fd_slots_) {
        throw std::out_of_range("Direct fd slot out of range or files not registered");
    }
    int ret = io_uring_register_files_update(&ring_, slot, &fd, 1);
    if (ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "io_uring_register_files_update failed");
    }
}

void IoUringEngine::remove_direct_fd(unsigned slot) noexcept {
    if (!files_registered_ || slot >= direct_fd_slots_) return;
    int minus_one = -1;
    (void)io_uring_register_files_update(&ring_, slot, &minus_one, 1);
}

} // namespace aegon::core
