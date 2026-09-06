#include "BufferPool.h"
#include <cstdlib>
#include <system_error>
#include <cstring>

namespace aegon::core {

BufferPool::BufferPool(struct io_uring* ring, uint16_t bgid, uint16_t entries, size_t buffer_size)
    : ring_(ring), bgid_(bgid), entries_(entries), buffer_size_(buffer_size) {
    
    // Check power-of-two requirement for io_uring_buf_ring
    if ((entries & (entries - 1)) != 0 || entries == 0) {
        throw std::invalid_argument("BufferPool entries must be a power of 2");
    }

    int ret = 0;
    buf_ring_ = io_uring_setup_buf_ring(ring_, entries_, bgid_, 0, &ret);
    if (!buf_ring_) {
        throw std::system_error(-ret, std::generic_category(), "io_uring_setup_buf_ring failed");
    }

    // Allocate page-aligned memory for the payload buffers
    size_t total_payload = static_cast<size_t>(entries_) * buffer_size_;
    void* mem_ptr = nullptr;
    if (posix_memalign(&mem_ptr, 4096, total_payload) != 0 || !mem_ptr) {
        io_uring_free_buf_ring(ring_, buf_ring_, entries_, bgid_);
        throw std::bad_alloc();
    }
    memory_ = static_cast<uint8_t*>(mem_ptr);

    // Populate the buffer ring with all buffer slots
    const int mask = io_uring_buf_ring_mask(entries_);
    for (uint16_t i = 0; i < entries_; ++i) {
        uint8_t* buf = memory_ + (static_cast<size_t>(i) * buffer_size_);
        io_uring_buf_ring_add(buf_ring_, buf, static_cast<unsigned int>(buffer_size_), i, mask, i);
    }
    io_uring_buf_ring_advance(buf_ring_, entries_);
}

BufferPool::~BufferPool() {
    if (buf_ring_ && ring_) {
        io_uring_free_buf_ring(ring_, buf_ring_, entries_, bgid_);
        buf_ring_ = nullptr;
    }
    if (memory_) {
        free(memory_);
        memory_ = nullptr;
    }
}

BufferPool::BufferPool(BufferPool&& other) noexcept
    : ring_(other.ring_),
      buf_ring_(other.buf_ring_),
      memory_(other.memory_),
      bgid_(other.bgid_),
      entries_(other.entries_),
      buffer_size_(other.buffer_size_) {
    other.ring_ = nullptr;
    other.buf_ring_ = nullptr;
    other.memory_ = nullptr;
}

BufferPool& BufferPool::operator=(BufferPool&& other) noexcept {
    if (this != &other) {
        if (buf_ring_ && ring_) {
            io_uring_free_buf_ring(ring_, buf_ring_, entries_, bgid_);
        }
        if (memory_) {
            free(memory_);
        }
        ring_ = other.ring_;
        buf_ring_ = other.buf_ring_;
        memory_ = other.memory_;
        bgid_ = other.bgid_;
        entries_ = other.entries_;
        buffer_size_ = other.buffer_size_;

        other.ring_ = nullptr;
        other.buf_ring_ = nullptr;
        other.memory_ = nullptr;
    }
    return *this;
}

} // namespace aegon::core
