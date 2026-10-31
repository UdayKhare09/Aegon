#pragma once

#include "core/engine/IoEngine.h"
#include <sys/epoll.h>
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>
#include <queue>
#include <memory>
#include <unordered_map>

namespace aegon::core {

class BufferPool;

/**
 * @brief High-performance Linux epoll engine backend.
 *
 * Implements the unified IoEngine interface using Linux epoll, eventfd,
 * and high-resolution monotonic timers. Fully compatible with Linux kernels < 6.1,
 * containerized Docker environments, and WSL2 where io_uring is restricted.
 *
 * Zero-allocation on the fast path with O(1) FD table lookup.
 */
class EpollEngine final : public IoEngine {
public:
    explicit EpollEngine(const EngineConfig& config = {}, BufferPool* pool = nullptr);
    ~EpollEngine() override;

    EpollEngine(const EpollEngine&) = delete;
    EpollEngine& operator=(const EpollEngine&) = delete;
    EpollEngine(EpollEngine&&) noexcept;
    EpollEngine& operator=(EpollEngine&&) noexcept;

    [[nodiscard]] EngineType type() const noexcept override { return EngineType::Epoll; }
    [[nodiscard]] std::string_view name() const noexcept override { return "epoll"; }

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

    void register_buffer_pool(BufferPool& pool, bool register_buffers = true) override;

    [[nodiscard]] static bool is_supported() noexcept { return true; }

private:
    enum class InterestType : uint8_t {
        None,
        Accept,
        RecvProvided,
        Recv,
        Send,
        SendZc,
        Splice,
        Connect,
        Poll,
        Recvmsg,
        MultishotAccept,
        MultishotRecv
    };

    struct FdSlot {
        IoAwaiter* in_awaiter{nullptr};
        IoAwaiter* out_awaiter{nullptr};
        InterestType in_type{InterestType::None};
        InterestType out_type{InterestType::None};
        uint32_t epoll_mask{0};
    };

    struct ReadyCompletion {
        IoAwaiter* awaiter{nullptr};
        int res{0};
        uint32_t flags{0};
    };

    struct TimerEntry {
        std::chrono::steady_clock::time_point deadline;
        TimeoutAwaiter* awaiter{nullptr};
        uint64_t id{0};

        bool operator>(const TimerEntry& other) const noexcept {
            return deadline > other.deadline;
        }
    };

    void init(const EngineConfig& config);
    void add_interest(int fd, uint32_t event_flag, IoAwaiter* aw, InterestType type) noexcept;
    void remove_interest(int fd, uint32_t event_flag) noexcept;
    void remove_all_interests(int fd) noexcept;
    void process_epoll_event(int fd, uint32_t events) noexcept;
    void check_timers() noexcept;

    int epfd_{-1};
    int wakeup_fd_{-1};
    BufferPool* buffer_pool_{nullptr};
    std::unique_ptr<BufferPool> owned_buffer_pool_{nullptr};

    std::vector<FdSlot> slots_{};
    std::vector<struct epoll_event> ep_events_{};
    std::vector<ReadyCompletion> ready_completions_{};
    std::deque<ReadyCompletion> posted_completions_{};
    std::mutex posted_mutex_{};

    std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> timers_{};
    uint64_t next_timer_id_{0};
};

} // namespace aegon::core
