#include "core/BufferPool.h"
#include "log/Logger.h"
#include <cstdlib>
#include <system_error>
#include <cstring>
#include <sys/mman.h>

#if defined(__linux__)
#include <liburing.h>
#endif

namespace aegon::core {

void BufferPool::init_freelist() {
    freelist_.resize(entries_);
    for (uint16_t i = 0; i < entries_; ++i) {
        freelist_[i] = i;
    }
    freelist_top_ = entries_;
}

void BufferPool::allocate_memory() {
    total_payload_ = static_cast<size_t>(entries_) * buffer_size_;

    // 1. mmap + MAP_POPULATE + MAP_HUGETLB (2 MB huge pages, pre-faulted)
    void* mem_ptr = mmap(nullptr, total_payload_,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB,
                         -1, 0);
    if (mem_ptr == MAP_FAILED) {
        // 2. Fallback: 4 KB pages, still pre-faulted
        mem_ptr = mmap(nullptr, total_payload_,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
                       -1, 0);
    }

    if (mem_ptr != MAP_FAILED) {
        memory_ = static_cast<uint8_t*>(mem_ptr);
        memory_is_mmap_ = true;
    } else {
        // 3. Fallback: posix_memalign
        if (posix_memalign(reinterpret_cast<void**>(&mem_ptr), 4096, total_payload_) != 0 || !mem_ptr) {
            throw std::bad_alloc();
        }
        memory_ = static_cast<uint8_t*>(mem_ptr);
        memory_is_mmap_ = false;
    }
}

BufferPool::BufferPool(uint16_t bgid, uint16_t entries, size_t buffer_size)
    : bgid_(bgid), entries_(entries), buffer_size_(buffer_size) {
    if ((entries & (entries - 1)) != 0 || entries == 0) {
        log::error("[BufferPool] BufferPool entries must be a power of 2, got {}", entries);
        throw std::invalid_argument("BufferPool entries must be a power of 2");
    }

    allocate_memory();
    init_freelist();
    log::debug("[BufferPool] Initialized user-mode pool bgid={}: entries={}, buf_size={} B, total_payload={} KB",
               bgid_, entries_, buffer_size_, total_payload_ / 1024);
}

BufferPool::BufferPool(struct io_uring* ring, uint16_t bgid, uint16_t entries,
                       size_t buffer_size, bool register_buffers)
    : BufferPool(bgid, entries, buffer_size) {
    if (ring) {
        register_with_io_uring(ring, register_buffers);
    }
}

void BufferPool::register_with_io_uring(struct io_uring* ring, bool register_buffers) {
#if defined(__linux__)
    if (!ring) return;
    ring_ = ring;

    int ret = 0;
    while (entries_ >= 1) {
        buf_ring_ = io_uring_setup_buf_ring(ring_, entries_, bgid_, 0, &ret);
        if (buf_ring_) break;
        if ((ret == -ENOMEM || ret == -EPERM) && entries_ > 1) {
            log::warn("[BufferPool] io_uring_setup_buf_ring returned {}, halving entries to {}", ret, entries_ / 2);
            entries_ /= 2;
        } else {
            break;
        }
    }
    if (!buf_ring_) {
        log::warn("[BufferPool] io_uring_setup_buf_ring failed: {}, falling back to user-space freelist", std::strerror(-ret));
        return;
    }

    if (register_buffers && ring_) {
        struct iovec iov{};
        iov.iov_base = memory_;
        iov.iov_len  = total_payload_;
        buffers_registered_ = (io_uring_register_buffers(ring_, &iov, 1) == 0);
    }

    const int mask = io_uring_buf_ring_mask(entries_);
    for (uint16_t i = 0; i < entries_; ++i) {
        uint8_t* buf = memory_ + (static_cast<size_t>(i) * buffer_size_);
        io_uring_buf_ring_add(buf_ring_, buf, static_cast<unsigned int>(buffer_size_), i, mask, i);
    }
    io_uring_buf_ring_advance(buf_ring_, entries_);
    log::debug("[BufferPool] Attached to io_uring bgid={}: registered={}", bgid_, buffers_registered_);
#else
    (void)ring;
    (void)register_buffers;
#endif
}

void BufferPool::unregister_from_io_uring() {
#if defined(__linux__)
    if (buffers_registered_ && ring_) {
        io_uring_unregister_buffers(ring_);
        buffers_registered_ = false;
    }
    if (buf_ring_ && ring_) {
        io_uring_free_buf_ring(ring_, buf_ring_, entries_, bgid_);
        buf_ring_ = nullptr;
    }
    ring_ = nullptr;
#endif
}

BufferPool::~BufferPool() {
    unregister_from_io_uring();
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
      ring_size_bytes_(other.ring_size_bytes_),
      total_payload_(other.total_payload_),
      buffers_registered_(other.buffers_registered_),
      memory_is_mmap_(other.memory_is_mmap_),
      pending_advance_(other.pending_advance_),
      freelist_(std::move(other.freelist_)),
      freelist_top_(other.freelist_top_) {
    other.ring_               = nullptr;
    other.buf_ring_           = nullptr;
    other.memory_             = nullptr;
    other.entries_            = 0;
    other.buffer_size_        = 0;
    other.ring_size_bytes_    = 0;
    other.total_payload_      = 0;
    other.buffers_registered_ = false;
    other.memory_is_mmap_     = false;
    other.pending_advance_    = 0;
    other.freelist_top_       = 0;
}

BufferPool& BufferPool::operator=(BufferPool&& other) noexcept {
    if (this != &other) {
        unregister_from_io_uring();
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
        ring_size_bytes_    = other.ring_size_bytes_;
        total_payload_      = other.total_payload_;
        buffers_registered_ = other.buffers_registered_;
        memory_is_mmap_     = other.memory_is_mmap_;
        pending_advance_    = other.pending_advance_;
        freelist_           = std::move(other.freelist_);
        freelist_top_       = other.freelist_top_;

        other.ring_               = nullptr;
        other.buf_ring_           = nullptr;
        other.memory_             = nullptr;
        other.entries_            = 0;
        other.buffer_size_        = 0;
        other.ring_size_bytes_    = 0;
        other.total_payload_      = 0;
        other.buffers_registered_ = false;
        other.memory_is_mmap_     = false;
        other.pending_advance_    = 0;
        other.freelist_top_       = 0;
    }
    return *this;
}

void BufferPool::return_buffer(uint16_t bid) noexcept {
    if (!memory_ || bid >= entries_) [[unlikely]] return;

#if defined(__linux__)
    if (buf_ring_) {
        io_uring_buf_ring_add(buf_ring_,
                              memory_ + (static_cast<size_t>(bid) * buffer_size_),
                              static_cast<unsigned int>(buffer_size_),
                              bid,
                              io_uring_buf_ring_mask(entries_),
                              pending_advance_);
        ++pending_advance_;
        return;
    }
#endif

    if (freelist_top_ < entries_) {
        freelist_[freelist_top_++] = bid;
    }
}

void BufferPool::flush_returns() noexcept {
#if defined(__linux__)
    if (pending_advance_ > 0 && buf_ring_) [[likely]] {
        io_uring_buf_ring_advance(buf_ring_, pending_advance_);
        pending_advance_ = 0;
    }
#endif
}

} // namespace aegon::core
