#include "BufferPool.h"
#include <cstdlib>
#include <system_error>
#include <cstring>
#include <sys/mman.h>

namespace aegon::core {

BufferPool::BufferPool(struct io_uring* ring, uint16_t bgid, uint16_t entries,
                       size_t buffer_size, bool register_buffers)
    : ring_(ring), bgid_(bgid), entries_(entries), buffer_size_(buffer_size) {

    // Check power-of-two requirement for io_uring_buf_ring
    if ((entries & (entries - 1)) != 0 || entries == 0) {
        throw std::invalid_argument("BufferPool entries must be a power of 2");
    }

    int ret = 0;
    while (entries_ >= 1) {
        buf_ring_ = io_uring_setup_buf_ring(ring_, entries_, bgid_, 0, &ret);
        if (buf_ring_) break;
        if ((ret == -ENOMEM || ret == -EPERM) && entries_ > 1) {
            entries_ /= 2;
        } else {
            break;
        }
    }
    if (!buf_ring_) {
        throw std::system_error(-ret, std::generic_category(), "io_uring_setup_buf_ring failed");
    }

    // Allocate page-aligned memory for payload buffers.
    // Preference order:
    //   1. mmap + MAP_POPULATE + MAP_HUGETLB (2 MB huge pages, pre-faulted)
    //      — eliminates per-packet page faults; ~512× fewer TLB entries for 2 MB pools
    //   2. mmap + MAP_POPULATE (4 KB pages, pre-faulted)
    //      — still eliminates page faults, just higher TLB pressure
    //   3. posix_memalign as last resort (no pre-faulting, no alignment guarantee beyond 4 KB)
    total_payload_ = static_cast<size_t>(entries_) * buffer_size_;

    void* mem_ptr = mmap(nullptr, total_payload_,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB,
                         -1, 0);
    if (mem_ptr == MAP_FAILED) {
        // Fallback: 4 KB pages, still pre-faulted to avoid runtime page faults
        mem_ptr = mmap(nullptr, total_payload_,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
                       -1, 0);
    }

    if (mem_ptr != MAP_FAILED) {
        memory_ = static_cast<uint8_t*>(mem_ptr);
        memory_is_mmap_ = true;
    } else {
        // Last resort: posix_memalign (4 KB aligned, no pre-faulting)
        if (posix_memalign(reinterpret_cast<void**>(&mem_ptr), 4096, total_payload_) != 0 || !mem_ptr) {
            io_uring_free_buf_ring(ring_, buf_ring_, entries_, bgid_);
            throw std::bad_alloc();
        }
        memory_ = static_cast<uint8_t*>(mem_ptr);
        memory_is_mmap_ = false;
    }

    // Pre-register memory slab with io_uring to avoid per-op page pinning & translation.
    // This is global per-ring (one registration slot 0). Graceful no-op if it fails
    // (unprivileged containers or low RLIMIT_MEMLOCK).
    if (register_buffers && ring_) {
        struct iovec iov{};
        iov.iov_base = memory_;
        iov.iov_len  = total_payload_;
        buffers_registered_ = (io_uring_register_buffers(ring_, &iov, 1) == 0);
    }

    // Populate the buffer ring with all buffer slots
    const int mask = io_uring_buf_ring_mask(entries_);
    for (uint16_t i = 0; i < entries_; ++i) {
        uint8_t* buf = memory_ + (static_cast<size_t>(i) * buffer_size_);
        io_uring_buf_ring_add(buf_ring_, buf, static_cast<unsigned int>(buffer_size_), i, mask, i);
    }
    io_uring_buf_ring_advance(buf_ring_, entries_);
}

BufferPool::~BufferPool() {
    if (buffers_registered_ && ring_) {
        io_uring_unregister_buffers(ring_);
        buffers_registered_ = false;
    }
    if (buf_ring_ && ring_) {
        io_uring_free_buf_ring(ring_, buf_ring_, entries_, bgid_);
        buf_ring_ = nullptr;
    }
    if (memory_) {
        if (memory_is_mmap_) {
            munmap(memory_, total_payload_);
        } else {
            free(memory_);
        }
        memory_ = nullptr;
    }
}

BufferPool::BufferPool(BufferPool&& other) noexcept
    : ring_(other.ring_),
      buf_ring_(other.buf_ring_),
      memory_(other.memory_),
      bgid_(other.bgid_),
      entries_(other.entries_),
      buffer_size_(other.buffer_size_),
      ring_size_bytes_(other.ring_size_bytes_),  // FIX: was missing in original
      total_payload_(other.total_payload_),
      buffers_registered_(other.buffers_registered_),
      memory_is_mmap_(other.memory_is_mmap_),
      pending_advance_(other.pending_advance_) {
    other.ring_             = nullptr;
    other.buf_ring_         = nullptr;
    other.memory_           = nullptr;
    other.entries_          = 0;
    other.buffer_size_      = 0;
    other.ring_size_bytes_  = 0;
    other.total_payload_    = 0;
    other.buffers_registered_ = false;
    other.memory_is_mmap_   = false;
    other.pending_advance_  = 0;
}

BufferPool& BufferPool::operator=(BufferPool&& other) noexcept {
    if (this != &other) {
        if (buffers_registered_ && ring_) {
            io_uring_unregister_buffers(ring_);
        }
        if (buf_ring_ && ring_) {
            io_uring_free_buf_ring(ring_, buf_ring_, entries_, bgid_);
        }
        if (memory_) {
            if (memory_is_mmap_) {
                munmap(memory_, total_payload_);
            } else {
                free(memory_);
            }
        }

        ring_               = other.ring_;
        buf_ring_           = other.buf_ring_;
        memory_             = other.memory_;
        bgid_               = other.bgid_;
        entries_            = other.entries_;
        buffer_size_        = other.buffer_size_;
        ring_size_bytes_    = other.ring_size_bytes_;  // FIX: was missing in original
        total_payload_      = other.total_payload_;
        buffers_registered_ = other.buffers_registered_;
        memory_is_mmap_     = other.memory_is_mmap_;
        pending_advance_    = other.pending_advance_;

        other.ring_             = nullptr;
        other.buf_ring_         = nullptr;
        other.memory_           = nullptr;
        other.entries_          = 0;
        other.buffer_size_      = 0;
        other.ring_size_bytes_  = 0;
        other.total_payload_    = 0;
        other.buffers_registered_ = false;
        other.memory_is_mmap_   = false;
        other.pending_advance_  = 0;
    }
    return *this;
}

} // namespace aegon::core
