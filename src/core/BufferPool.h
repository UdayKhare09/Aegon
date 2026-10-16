#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <memory>
#include <stdexcept>
#include <vector>

struct io_uring;
struct io_uring_buf_ring;

namespace aegon::core {

/**
 * @brief High-performance dual-mode memory buffer pool.
 *
 * Supports two operational modes:
 *  1. Kernel PBUF_RING (Linux io_uring): Linux kernel automatically selects buffers
 *     on incoming packets, zero kernel-user round trips.
 *  2. User-Space Freelist (epoll / kqueue / IOCP): Fast $O(1)$ lockless slab allocation
 *     from pre-faulted, cache-aligned memory.
 *
 * Memory is allocated via mmap with MAP_POPULATE and MAP_HUGETLB (2 MB huge pages,
 * with 4 KB fallback), drastically minimizing TLB cache pressure.
 */
class BufferPool {
public:
    static constexpr size_t   DEFAULT_BUFFER_SIZE = 4096; // 4 KB per buffer
    static constexpr uint16_t DEFAULT_ENTRIES     = 2048; // Must be power of 2

    explicit BufferPool(uint16_t bgid = 1,
                        uint16_t entries = DEFAULT_ENTRIES,
                        size_t buffer_size = DEFAULT_BUFFER_SIZE);

    BufferPool(struct io_uring* ring, uint16_t bgid,
               uint16_t entries = DEFAULT_ENTRIES,
               size_t buffer_size = DEFAULT_BUFFER_SIZE,
               bool register_buffers = true);

    ~BufferPool();

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) noexcept;
    BufferPool& operator=(BufferPool&&) noexcept;

    // Attach to an active io_uring instance
    void register_with_io_uring(struct io_uring* ring, bool register_buffers = true);
    void unregister_from_io_uring();

    // User-space allocation (used by EpollEngine)
    [[nodiscard]] inline int alloc_buffer() noexcept {
        if (freelist_top_ > 0) {
            return freelist_[--freelist_top_];
        }
        return -1; // Out of buffers (ENOBUFS)
    }

    // Returns a slice to the buffer corresponding to the buffer ID
    [[nodiscard]] inline std::span<uint8_t> get_buffer(uint16_t bid, int bytes) const noexcept {
        if (!memory_ || bid >= entries_ || bytes < 0) [[unlikely]] return {};
        uint8_t* ptr = memory_ + (static_cast<size_t>(bid) * buffer_size_);
        size_t len = std::min(static_cast<size_t>(bytes), buffer_size_);
        return {ptr, len};
    }

    /**
     * Return a consumed buffer back to the pool.
     */
    void return_buffer(uint16_t bid) noexcept;

    /**
     * Flush all pending buffer returns to the kernel (io_uring mode).
     * In user-space mode, this is a no-op.
     */
    void flush_returns() noexcept;

    [[nodiscard]] uint16_t bgid() const noexcept { return bgid_; }
    [[nodiscard]] size_t buffer_size() const noexcept { return buffer_size_; }
    [[nodiscard]] uint16_t entries() const noexcept { return entries_; }
    [[nodiscard]] bool is_registered() const noexcept { return buffers_registered_; }
    [[nodiscard]] bool is_io_uring_mode() const noexcept { return buf_ring_ != nullptr; }

    [[nodiscard]] int registered_buf_index() const noexcept {
        return buffers_registered_ ? 0 : -1;
    }

    [[nodiscard]] uint8_t* raw_memory() noexcept { return memory_; }
    [[nodiscard]] const uint8_t* raw_memory() const noexcept { return memory_; }

private:
    void allocate_memory();
    void init_freelist();

    struct io_uring* ring_{nullptr};
    struct io_uring_buf_ring* buf_ring_{nullptr};
    uint8_t* memory_{nullptr};
    uint16_t bgid_{1};
    uint16_t entries_{DEFAULT_ENTRIES};
    size_t buffer_size_{DEFAULT_BUFFER_SIZE};
    size_t ring_size_bytes_{0};
    size_t total_payload_{0};
    bool buffers_registered_{false};
    bool memory_is_mmap_{false};
    int pending_advance_{0};

    // User-space freelist for epoll/kqueue/IOCP
    std::vector<uint16_t> freelist_{};
    size_t freelist_top_{0};
};

} // namespace aegon::core
