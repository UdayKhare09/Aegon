#pragma once

#include <liburing.h>
#include <cstdint>
#include <cstddef>
#include <span>
#include <memory>
#include <stdexcept>
#include <vector>

namespace aegon::core {

/**
 * @brief High-performance kernel-managed Provided Buffer Ring (PBUF_RING).
 *
 * Registered with io_uring so the Linux kernel automatically selects buffers
 * on incoming network packets, eliminating round-trip buffer allocation overhead.
 *
 * Memory is allocated via mmap with MAP_POPULATE (pre-fault) and MAP_HUGETLB
 * (2 MB huge pages, with 4 KB fallback), reducing TLB pressure under heavy load.
 *
 * Buffer returns are batched: call return_buffer() for each consumed buffer,
 * then flush_returns() once per event-loop iteration to advance the ring tail
 * in a single operation instead of N individual advances.
 */
class BufferPool {
public:
    static constexpr size_t DEFAULT_BUFFER_SIZE = 4096; // 4 KB per buffer
    static constexpr uint16_t DEFAULT_ENTRIES = 2048;   // Must be power of 2

    BufferPool(struct io_uring* ring, uint16_t bgid,
               uint16_t entries = DEFAULT_ENTRIES,
               size_t buffer_size = DEFAULT_BUFFER_SIZE,
               bool register_buffers = true);

    ~BufferPool();

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) noexcept;
    BufferPool& operator=(BufferPool&&) noexcept;

    // Returns a slice to the buffer corresponding to the kernel-returned buffer ID
    [[nodiscard]] inline std::span<uint8_t> get_buffer(uint16_t bid, int bytes) const noexcept {
        if (!memory_ || bid >= entries_ || bytes < 0) [[unlikely]] return {};
        uint8_t* ptr = memory_ + (static_cast<size_t>(bid) * buffer_size_);
        size_t len = std::min(static_cast<size_t>(bytes), buffer_size_);
        return {ptr, len};
    }

    /**
     * Queue a consumed buffer back to the kernel ring.
     *
     * Does NOT advance the ring tail immediately — batch multiple return_buffer()
     * calls together and commit them with a single flush_returns() call to avoid
     * one cache-line write per returned buffer.
     */
    inline void return_buffer(uint16_t bid) noexcept {
        if (!buf_ring_ || !memory_ || bid >= entries_) [[unlikely]] {
            return;
        }
        io_uring_buf_ring_add(buf_ring_,
                              memory_ + (static_cast<size_t>(bid) * buffer_size_),
                              static_cast<unsigned int>(buffer_size_),
                              bid,
                              io_uring_buf_ring_mask(entries_),
                              pending_advance_);
        ++pending_advance_;
    }

    /**
     * Flush all pending buffer returns to the kernel by advancing the ring tail.
     * Call once per event-loop iteration after all return_buffer() calls.
     */
    inline void flush_returns() noexcept {
        if (pending_advance_ > 0 && buf_ring_) [[likely]] {
            io_uring_buf_ring_advance(buf_ring_, pending_advance_);
            pending_advance_ = 0;
        }
    }

    [[nodiscard]] uint16_t bgid() const noexcept { return bgid_; }
    [[nodiscard]] size_t buffer_size() const noexcept { return buffer_size_; }
    [[nodiscard]] uint16_t entries() const noexcept { return entries_; }
    [[nodiscard]] bool is_registered() const noexcept { return buffers_registered_; }

    /**
     * Returns the registered buffer index (0) if the memory was successfully
     * pinned via io_uring_register_buffers, or -1 if not registered.
     * Used by send_zc_fixed to reference pre-registered memory.
     */
    [[nodiscard]] int registered_buf_index() const noexcept {
        return buffers_registered_ ? 0 : -1;
    }

    [[nodiscard]] uint8_t* raw_memory() noexcept { return memory_; }
    [[nodiscard]] const uint8_t* raw_memory() const noexcept { return memory_; }

private:
    struct io_uring* ring_{nullptr};
    struct io_uring_buf_ring* buf_ring_{nullptr};
    uint8_t* memory_{nullptr};
    uint16_t bgid_{0};
    uint16_t entries_{0};
    size_t buffer_size_{0};
    size_t ring_size_bytes_{0};
    size_t total_payload_{0};   // tracks mmap size for munmap in destructor
    bool buffers_registered_{false};
    bool memory_is_mmap_{false}; // true when memory was allocated via mmap
    int pending_advance_{0};     // batched return_buffer count, flushed by flush_returns()
};

} // namespace aegon::core
