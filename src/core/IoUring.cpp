#include "IoUring.h"
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace aegon::core {

// ---------------------------------------------------------------------------
// IoUring::init — core ring setup with all Linux 6.1 optimizations
// ---------------------------------------------------------------------------

void IoUring::init(const IoUringConfig& config) {
    struct io_uring_params params{};

    // Modern Linux 6.x thread-per-core flags (all available on 6.1):
    //
    // IORING_SETUP_SINGLE_ISSUER  (6.0): lockless SQ for single-threaded loops.
    // IORING_SETUP_DEFER_TASKRUN  (6.1): task work runs ONLY inside submit_and_wait,
    //   completely eliminating inter-core IPI storms from implicit task-work delivery.
    //   Subsumes COOP_TASKRUN + TASKRUN_FLAG — no need to set those separately.
    // IORING_SETUP_SUBMIT_ALL     (5.18): submit entire SQ batch unconditionally.
    params.flags = config.flags |
                   IORING_SETUP_SINGLE_ISSUER |
#ifdef IORING_SETUP_DEFER_TASKRUN
                   IORING_SETUP_DEFER_TASKRUN |
#else
                   // Older liburing fallback — still better than nothing
                   IORING_SETUP_COOP_TASKRUN |
                   IORING_SETUP_TASKRUN_FLAG |
#endif
                   IORING_SETUP_SUBMIT_ALL;

    int ret = io_uring_queue_init_params(config.entries, &ring_, &params);
    if (ret != 0) {
        throw std::system_error(-ret, std::generic_category(),
                                "io_uring_queue_init_params failed");
    }
    initialized_ = true;

    // io_uring_ring_dontfork (5.6): prevent child processes from inheriting
    // the ring fd on fork(). Accidental inheritance can corrupt the ring state.
    (void)io_uring_ring_dontfork(&ring_);

    // io_uring_register_ring_fd (5.18): register the ring's own fd with the ring,
    // saving one syscall round-trip per io_uring_enter call. Best-effort.
    (void)io_uring_register_ring_fd(&ring_);

    // Register a sparse direct fd table if requested
    if (config.direct_fd_count > 0) {
        register_direct_fds(config.direct_fd_count);
    }
}

IoUring::IoUring(uint32_t entries, uint32_t flags) {
    IoUringConfig cfg;
    cfg.entries = entries;
    cfg.flags   = flags;
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
      files_registered_(other.files_registered_),
      direct_fd_slots_(other.direct_fd_slots_) {
    other.initialized_     = false;
    other.files_registered_ = false;
    other.direct_fd_slots_  = 0;
}

IoUring& IoUring::operator=(IoUring&& other) noexcept {
    if (this != &other) {
        if (initialized_) {
            io_uring_queue_exit(&ring_);
        }
        ring_              = other.ring_;
        initialized_       = other.initialized_;
        files_registered_  = other.files_registered_;
        direct_fd_slots_   = other.direct_fd_slots_;
        other.initialized_     = false;
        other.files_registered_ = false;
        other.direct_fd_slots_  = 0;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// SQE acquisition
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Direct fd table
// ---------------------------------------------------------------------------

bool IoUring::register_direct_fds(unsigned count) noexcept {
    if (!initialized_ || count == 0 || files_registered_) return false;
    int ret = io_uring_register_files_sparse(&ring_, count);
    if (ret == 0) {
        files_registered_  = true;
        direct_fd_slots_   = count;
        return true;
    }
    return false;
}

void IoUring::update_direct_fd(unsigned slot, int fd) {
    if (!files_registered_ || slot >= direct_fd_slots_) {
        throw std::out_of_range("Direct fd slot out of range");
    }
    int fds[1] = {fd};
    int ret = io_uring_register_files_update(&ring_, slot, fds, 1);
    if (ret < 0) {
        throw std::system_error(-ret, std::generic_category(),
                                "io_uring_register_files_update failed");
    }
}

void IoUring::remove_direct_fd(unsigned slot) noexcept {
    if (!files_registered_ || slot >= direct_fd_slots_) return;
    int minus_one = -1;
    (void)io_uring_register_files_update(&ring_, slot, &minus_one, 1);
}

// ---------------------------------------------------------------------------
// One-shot Accept
// ---------------------------------------------------------------------------

void IoUring::AcceptAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        IoUring::resume_continuation(*this);
        return;
    }
    io_uring_prep_accept(sqe, listen_fd,
                         reinterpret_cast<struct sockaddr*>(&addr),
                         &addr_len, 0);
    io_uring_sqe_set_data(sqe, this);
}

AcceptResult IoUring::AcceptAwaiter::await_resume() noexcept {
    AcceptResult res;
    res.fd       = result;
    res.addr     = addr;
    res.addr_len = addr_len;
    res.has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;
    return res;
}

// ---------------------------------------------------------------------------
// MultishotAcceptStream
// ---------------------------------------------------------------------------

MultishotAcceptStream::StreamState::~StreamState() {
    // Close any buffered fds we never handed off
    for (const auto& res : backlog) {
        if (res.fd >= 0 && !res.is_direct_fd) {
            ::close(res.fd);
        }
    }
    backlog.clear();
}

void MultishotAcceptStream::StreamState::submit() noexcept {
    if (armed) return;
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        finished = true;
        IoUring::resume_continuation(*this);
        return;
    }
    io_uring_prep_multishot_accept(sqe, listen_fd, nullptr, nullptr, 0);
    if (use_direct) {
        // Auto-allocate a slot in the registered direct fd table (Linux 5.19+)
        sqe->file_index = IORING_FILE_INDEX_ALLOC;
    }
    io_uring_sqe_set_data(sqe, this);
    armed  = true;
    self_  = shared_from_this();
}

void MultishotAcceptStream::StreamState::on_completion(int res, uint32_t flags) noexcept {
    AcceptResult ar;
    ar.fd           = res;
    ar.has_more     = (flags & IORING_CQE_F_MORE) != 0;
    ar.is_direct_fd = use_direct;

    if (res < 0) {
        // Hard error — terminate the stream
        armed    = false;
        finished = true;
    } else if (!ar.has_more) {
        // Kernel temporarily disarmed (SQ full during re-arm, etc.)
        // Do NOT set finished — re-arm on next await_suspend()
        armed = false;
    }
    // (res >= 0 && has_more): still armed, nothing to change

    if (continuation && !continuation.done()) {
        current_result = ar;
        auto cont = continuation;
        continuation = nullptr;
        cont.resume();
    } else if (res >= 0) {
        // No waiter — buffer the result (only for valid fds)
        backlog.push_back(ar);
    }

    if (!armed) {
        self_.reset();
    }
}

MultishotAcceptStream::MultishotAcceptStream(IoUring& ring, int listen_fd, bool use_direct)
    : state_(std::make_shared<StreamState>(ring, listen_fd, use_direct)) {}

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
        state_->armed     = false;
        state_->cancelled = true;
        state_->finished  = true;
    }
}

// ---------------------------------------------------------------------------
// MultishotRecvStream
// ---------------------------------------------------------------------------

MultishotRecvStream::StreamState::~StreamState() {
    backlog.clear();
}

void MultishotRecvStream::StreamState::submit() noexcept {
    if (armed || finished || cancelled) return;
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result   = -EBUSY;
        finished = true;
        IoUring::resume_continuation(*this);
        return;
    }
    io_uring_prep_recv_multishot(sqe, socket_fd, nullptr, 0, 0);
    sqe->flags    |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = bgid;
    io_uring_sqe_set_data(sqe, this);
    armed = true;
    self_ = shared_from_this();
}

void MultishotRecvStream::StreamState::on_completion(int res, uint32_t flags) noexcept {
    RecvResult rr;
    rr.bytes        = res;
    rr.has_more     = (flags & IORING_CQE_F_MORE) != 0;
    rr.buffer_valid = (flags & IORING_CQE_F_BUFFER) != 0;

    if (rr.buffer_valid) {
        rr.bid = static_cast<uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
    }

    if (res < 0) {
        // Hard error — terminate stream; no valid buffer to return
        armed    = false;
        finished = true;
        rr.eof   = false;
    } else if (res == 0) {
        // Clean EOF from peer
        armed    = false;
        finished = true;
        rr.eof   = true;
    } else if (!rr.has_more) {
        // Kernel temporarily disarmed (e.g., ENOBUFS on buffer ring, transient SQ pressure)
        // Do NOT set finished — re-arm on next await_suspend()
        armed = false;
    }
    // (res > 0 && has_more): still armed, kernel will deliver more CQEs

    // Only forward to consumer if there's actual data, or if the stream terminated
    // with a clean EOF (res == 0, no buffer ID). On hard error (res < 0), only
    // forward if there's no buffer associated (rr.buffer_valid is false, bid stays 0).
    bool should_deliver = (res > 0) || (res <= 0 && rr.eof) || (res < 0);

    if (should_deliver) {
        if (continuation && !continuation.done()) {
            current_result = rr;
            auto cont = continuation;
            continuation = nullptr;
            cont.resume();
        } else if (res > 0) {
            // Only buffer results with valid data (avoids backlog with invalid bids)
            backlog.push_back(rr);
        }
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
        state_->armed     = false;
        state_->cancelled = true;
        state_->finished  = true;
    }
}

// ---------------------------------------------------------------------------
// One-shot RecvProvided (PBUF_RING)
// ---------------------------------------------------------------------------

void IoUring::RecvProvidedAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_recv(sqe, socket_fd, nullptr, 0, 0);
    sqe->flags    |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = bgid;
    io_uring_sqe_set_data(sqe, this);
}

RecvResult IoUring::RecvProvidedAwaiter::await_resume() noexcept {
    RecvResult res;
    res.bytes        = result;
    res.buffer_valid = (cqe_flags & IORING_CQE_F_BUFFER) != 0;
    if (res.buffer_valid) {
        res.bid = static_cast<uint16_t>(cqe_flags >> IORING_CQE_BUFFER_SHIFT);
    }
    res.has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;
    res.eof      = (result == 0);
    return res;
}

// ---------------------------------------------------------------------------
// RecvAwaiter
// ---------------------------------------------------------------------------

void IoUring::RecvAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_recv(sqe, socket_fd, buf, len, flags);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::RecvAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// ConnectAwaiter
// ---------------------------------------------------------------------------

void IoUring::ConnectAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
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

// ---------------------------------------------------------------------------
// SendAwaiter
// ---------------------------------------------------------------------------

void IoUring::SendAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_send(sqe, socket_fd, buf, len, MSG_NOSIGNAL);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::SendAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// SendZcAwaiter
// ---------------------------------------------------------------------------

void IoUring::SendZcAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
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
        result    = res;
        cqe_flags = flags;
        resume_continuation(*this);
        return;
    }

    if (flags & IORING_CQE_F_NOTIF) {
        notif_received = true;
    } else {
        bytes_sent     = res;
        send_completed = true;
        if (!(flags & IORING_CQE_F_MORE)) {
            // No notification CQE expected — copy path taken
            notif_received = true;
        }
    }

    if (send_completed && notif_received) {
        result    = bytes_sent;
        cqe_flags = flags;
        resume_continuation(*this);
    }
}

int IoUring::SendZcAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// SpliceAwaiter
// ---------------------------------------------------------------------------

void IoUring::SpliceAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_splice(sqe, fd_in, off_in, fd_out, off_out, nbytes, splice_flags);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::SpliceAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// CloseAwaiter
// ---------------------------------------------------------------------------

void IoUring::CloseAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_close(sqe, fd);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::CloseAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// CancelAwaiter
// ---------------------------------------------------------------------------

void IoUring::CancelAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_cancel64(sqe, user_data, cancel_flags);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::CancelAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// RecvmsgAwaiter
// ---------------------------------------------------------------------------

void IoUring::RecvmsgAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_recvmsg(sqe, socket_fd, msg, 0);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::RecvmsgAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// TimeoutAwaiter
// ---------------------------------------------------------------------------

void IoUring::TimeoutAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_timeout(sqe, &ts, 0, 0);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::TimeoutAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// PollAwaiter
// ---------------------------------------------------------------------------

void IoUring::PollAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_poll_add(sqe, fd, poll_mask);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::PollAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// ShutdownAwaiter (Linux 5.11+)
// ---------------------------------------------------------------------------

void IoUring::ShutdownAwaiter::submit() noexcept {
    struct io_uring_sqe* sqe = ring.acquire_sqe();
    if (!sqe) [[unlikely]] {
        result = -EBUSY;
        resume_continuation(*this);
        return;
    }
    io_uring_prep_shutdown(sqe, fd, how);
    io_uring_sqe_set_data(sqe, this);
}

int IoUring::ShutdownAwaiter::await_resume() noexcept {
    return result;
}

// ---------------------------------------------------------------------------
// process_completions
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// submit_and_wait
// ---------------------------------------------------------------------------

int IoUring::submit_and_wait(uint32_t min_complete) {
    int ret = io_uring_submit_and_wait(&ring_, min_complete);
    if (ret < 0 && ret != -EINTR) {
        throw std::system_error(-ret, std::generic_category(),
                                "io_uring_submit_and_wait failed");
    }
    return ret;
}

// ---------------------------------------------------------------------------
// send_all helpers
// ---------------------------------------------------------------------------

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
