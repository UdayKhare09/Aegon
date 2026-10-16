#pragma once

#if defined(_WIN32)

#include "core/engine/IoEngine.h"
#include "core/BufferPool.h"
#include <vector>
#include <queue>
#include <chrono>
#include <system_error>

namespace aegon::core {

/**
 * @brief High-performance Windows I/O Completion Port (IOCP) engine backend.
 *
 * Implements the unified IoEngine interface using native Windows NT completion ports:
 *  - Multishot completion harvesting via GetQueuedCompletionStatusEx (up to 128 completions/call).
 *  - Non-blocking overlapped accepts via AcceptEx + GetAcceptExSockaddrs.
 *  - Overlapped non-blocking TCP connect via ConnectEx.
 *  - Native zero-copy file-to-socket transfer via TransmitFile.
 *  - Thread-safe zero-overhead cross-thread wakeup via PostQueuedCompletionStatus.
 *  - Zero-heap allocation per awaiter: OVERLAPPED is embedded in IoAwaiter.
 */
class IocpEngine final : public IoEngine {
public:
    explicit IocpEngine(const EngineConfig& config = {}, BufferPool* pool = nullptr);
    ~IocpEngine() override;

    IocpEngine(const IocpEngine&) = delete;
    IocpEngine& operator=(const IocpEngine&) = delete;
    IocpEngine(IocpEngine&&) = delete;
    IocpEngine& operator=(IocpEngine&&) = delete;

    [[nodiscard]] EngineType type() const noexcept override { return EngineType::Iocp; }
    [[nodiscard]] std::string_view name() const noexcept override { return "iocp"; }

    // Loop iteration
    int submit_and_wait(uint32_t min_complete = 1) override;
    size_t process_completions() noexcept override;
    void wakeup() noexcept override;
    void post_completion(IoAwaiter* aw, int res = 0) noexcept override;

    // Low-level submit dispatchers invoked by awaiters
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
    void submit_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept override;
    void cancel_multishot_accept(MultishotAcceptStream::StreamState& st) noexcept override;
    void submit_multishot_recv(MultishotRecvStream::StreamState& st) noexcept override;
    void cancel_multishot_recv(MultishotRecvStream::StreamState& st) noexcept override;

    void register_buffer_pool(BufferPool& pool, bool register_buffers = true) override {
        (void)register_buffers;
        buffer_pool_ = &pool;
    }

    /**
     * @brief Associate a newly created or accepted socket with this engine's IOCP.
     */
    bool associate_socket(os_socket_t sock) noexcept;

private:
    void init_iocp(uint32_t entries);
    void load_extension_functions();
    void process_timers() noexcept;

    static constexpr ULONG_PTR WAKEUP_KEY = 0xDEADBEEFULL;
    static constexpr size_t MAX_BATCH_COMPLETIONS = 128;

    HANDLE iocp_{INVALID_HANDLE_VALUE};
    BufferPool* buffer_pool_{nullptr};
    bool initialized_{false};

    // Windows Socket Extension Function Pointers
    LPFN_ACCEPTEX fn_accept_ex_{nullptr};
    LPFN_GETACCEPTEXSOCKADDRS fn_get_accept_ex_sockaddrs_{nullptr};
    LPFN_CONNECTEX fn_connect_ex_{nullptr};
    LPFN_TRANSMITFILE fn_transmit_file_{nullptr};

    // Staging array for GetQueuedCompletionStatusEx
    OVERLAPPED_ENTRY completion_entries_[MAX_BATCH_COMPLETIONS]{};
    ULONG completed_count_{0};

    // Monotonic Timer Min-Heap
    struct TimerEntry {
        std::chrono::steady_clock::time_point deadline;
        TimeoutAwaiter* awaiter;

        bool operator>(const TimerEntry& o) const noexcept {
            return deadline > o.deadline;
        }
    };
    std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> timers_{};
};

} // namespace aegon::core

#endif // _WIN32
