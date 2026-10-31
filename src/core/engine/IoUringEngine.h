#pragma once

#include "core/engine/IoEngine.h"
#include <liburing.h>
#include <unordered_set>
#include <vector>
#include <deque>
#include <mutex>

namespace aegon::core {

class BufferPool;

/**
 * @brief Linux 6.1+ high-performance io_uring engine backend.
 *
 * Implements the unified IoEngine interface using Linux io_uring with
 * IORING_SETUP_DEFER_TASKRUN, IORING_SETUP_SINGLE_ISSUER, multishot accept/recv,
 * direct file tables, and kernel buffer rings (PBUF_RING).
 */
class IoUringEngine final : public IoEngine {
public:
    explicit IoUringEngine(const EngineConfig& config = {}, BufferPool* pool = nullptr);
    ~IoUringEngine() override;

    IoUringEngine(const IoUringEngine&) = delete;
    IoUringEngine& operator=(const IoUringEngine&) = delete;
    IoUringEngine(IoUringEngine&&) noexcept;
    IoUringEngine& operator=(IoUringEngine&&) noexcept;

    [[nodiscard]] EngineType type() const noexcept override { return EngineType::IoUring; }
    [[nodiscard]] std::string_view name() const noexcept override { return "io_uring"; }

    int submit_and_wait(uint32_t min_complete = 1) override;
    size_t process_completions() noexcept override;
    void wakeup() noexcept override;

    void submit_accept(AcceptAwaiter& aw) noexcept override;
    void submit_recv_provided(RecvProvidedAwaiter& aw) noexcept override;
    void submit_recv(RecvAwaiter& aw) noexcept override;
    void submit_send(SendAwaiter& aw) noexcept override;
    void submit_send_zc(SendZcAwaiter& aw) noexcept override;
    void submit_splice(SpliceAwaiter& aw) noexcept override;
    void submit_connect(ConnectAwaiter& aw) noexcept override;
    void submit_poll(PollAwaiter& aw) noexcept override;
    void submit_timeout(TimeoutAwaiter& aw) noexcept override;
    void submit_close(CloseAwaiter& aw) noexcept override;
    void submit_shutdown(ShutdownAwaiter& aw) noexcept override;
    void submit_cancel(CancelAwaiter& aw) noexcept override;
    void submit_recvmsg(RecvmsgAwaiter& aw) noexcept override;
    void submit_read(ReadAwaiter& aw) noexcept override;
    void submit_write(WriteAwaiter& aw) noexcept override;
    void submit_fsync(FsyncAwaiter& aw) noexcept override;
    void submit_fallocate(FallocateAwaiter& aw) noexcept override;
    void post_completion(IoAwaiter* aw, int res = 0) noexcept override;

    void submit_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept override;
    void cancel_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept override;
    void submit_multishot_recv(MultishotRecvStream::StreamState& st) noexcept override;
    void cancel_multishot_recv(MultishotRecvStream::StreamState& st) noexcept override;

    bool register_direct_fds(unsigned count) noexcept override;
    void update_direct_fd(unsigned slot, int fd) override;
    void remove_direct_fd(unsigned slot) noexcept override;
    [[nodiscard]] bool has_direct_fds() const noexcept override { return files_registered_; }
    [[nodiscard]] unsigned direct_fd_slots() const noexcept override { return direct_fd_slots_; }

    [[nodiscard]] MultishotAcceptStream accept_multishot_direct(int listen_fd) noexcept override {
        return MultishotAcceptStream{*this, listen_fd, true};
    }

    [[nodiscard]] struct io_uring* raw_ring() noexcept { return &ring_; }
    [[nodiscard]] struct io_uring_sqe* acquire_sqe() noexcept;
    void register_buffer_pool(BufferPool& pool, bool register_buffers = true) override;

private:
    void init(const EngineConfig& config);
    void arm_wakeup() noexcept;

    struct WakeupAwaiter : IoAwaiter {
        IoUringEngine& engine;
        explicit WakeupAwaiter(IoUringEngine& e) noexcept : engine(e) {}
        void submit() noexcept override;
        void on_completion(int res, uint32_t flags) noexcept override;
    } wakeup_awaiter_{*this};

    struct io_uring ring_{};
    bool initialized_{false};
    bool files_registered_{false};
    unsigned direct_fd_slots_{0};
    int wakeup_fd_{-1};
    std::unordered_set<uint64_t> cancelled_user_data_{};

    struct PostedCompletion {
        IoAwaiter* awaiter{nullptr};
        int res{0};
    };
    std::deque<PostedCompletion> posted_completions_{};
    std::mutex posted_mutex_{};
};

} // namespace aegon::core
