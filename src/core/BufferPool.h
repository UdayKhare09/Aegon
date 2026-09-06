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
 */
class BufferPool {
public:
    static constexpr size_t DEFAULT_BUFFER_SIZE = 4096; // 4KB per buffer
    static constexpr uint16_t DEFAULT_ENTRIES = 2048;   // Must be power of 2

    BufferPool(struct io_uring* ring, uint16_t bgid, 
               uint16_t entries = DEFAULT_ENTRIES, 
               size_t buffer_size = DEFAULT_BUFFER_SIZE);

    ~BufferPool();

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) noexcept;
    BufferPool& operator=(BufferPool&&) noexcept;

    // Returns a slice to the buffer corresponding to the kernel-returned buffer ID
    [[nodiscard]] inline std::span<uint8_t> get_buffer(uint16_t bid, int bytes) const noexcept {
        if (bid >= entries_) [[unlikely]] return {};
        uint8_t* ptr = memory_ + (static_cast<size_t>(bid) * buffer_size_);
        size_t len = bytes > 0 ? static_cast<size_t>(bytes) : 0;
        return {ptr, len};
    }

    // Return a consumed buffer back to the kernel ring
    inline void return_buffer(uint16_t bid) noexcept {
        io_uring_buf_ring_add(buf_ring_, 
                              memory_ + (static_cast<size_t>(bid) * buffer_size_),
                              static_cast<unsigned int>(buffer_size_), 
                              bid, 
                              io_uring_buf_ring_mask(entries_), 
                              0);
        io_uring_buf_ring_advance(buf_ring_, 1);
    }

    [[nodiscard]] uint16_t bgid() const noexcept { return bgid_; }
    [[nodiscard]] size_t buffer_size() const noexcept { return buffer_size_; }
    [[nodiscard]] uint16_t entries() const noexcept { return entries_; }

private:
    struct io_uring* ring_{nullptr};
    struct io_uring_buf_ring* buf_ring_{nullptr};
    uint8_t* memory_{nullptr};
    uint16_t bgid_{0};
    uint16_t entries_{0};
    size_t buffer_size_{0};
    size_t ring_size_bytes_{0};
};

} // namespace aegon::core
