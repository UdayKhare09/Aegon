#pragma once

#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <span>
#include <memory>
#include <algorithm>
#include <new>

namespace aegon::storage {

/**
 * @brief Memory buffer guaranteed to meet sector-level alignment for O_DIRECT NVMe operations.
 */
class AlignedBuffer {
public:
    explicit AlignedBuffer(size_t capacity = 0, size_t alignment = 4096)
        : alignment_(alignment > 0 ? alignment : 4096) {
        if (capacity > 0) {
            allocate(capacity);
        }
    }

    ~AlignedBuffer() {
        release();
    }

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    AlignedBuffer(AlignedBuffer&& other) noexcept
        : ptr_(other.ptr_), size_(other.size_), capacity_(other.capacity_), alignment_(other.alignment_) {
        other.ptr_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            alignment_ = other.alignment_;
            other.ptr_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    void allocate(size_t capacity) {
        if (capacity == capacity_ && ptr_) return;
        release();

        size_t alloc_size = ((capacity + alignment_ - 1) / alignment_) * alignment_;
        if (alloc_size == 0) alloc_size = alignment_;

#if defined(_WIN32)
        ptr_ = _aligned_malloc(alloc_size, alignment_);
        if (!ptr_) throw std::bad_alloc();
#else
        void* p = nullptr;
        if (::posix_memalign(&p, alignment_, alloc_size) != 0 || !p) {
            throw std::bad_alloc();
        }
        ptr_ = p;
#endif
        capacity_ = alloc_size;
        size_ = 0;
    }

    void resize(size_t new_size) {
        if (new_size > capacity_) {
            allocate(new_size);
        }
        size_ = new_size;
    }

    void clear() noexcept {
        size_ = 0;
    }

    [[nodiscard]] void* data() noexcept { return ptr_; }
    [[nodiscard]] const void* data() const noexcept { return ptr_; }

    [[nodiscard]] std::byte* byte_data() noexcept { return static_cast<std::byte*>(ptr_); }
    [[nodiscard]] const std::byte* byte_data() const noexcept { return static_cast<const std::byte*>(ptr_); }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t alignment() const noexcept { return alignment_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] std::span<std::byte> span() noexcept {
        return std::span<std::byte>(byte_data(), size_);
    }

    [[nodiscard]] std::span<const std::byte> span() const noexcept {
        return std::span<const std::byte>(byte_data(), size_);
    }

    [[nodiscard]] std::span<std::byte> full_capacity_span() noexcept {
        return std::span<std::byte>(byte_data(), capacity_);
    }

private:
    void release() noexcept {
        if (ptr_) {
#if defined(_WIN32)
            _aligned_free(ptr_);
#else
            ::free(ptr_);
#endif
            ptr_ = nullptr;
        }
        size_ = 0;
        capacity_ = 0;
    }

    void* ptr_{nullptr};
    size_t size_{0};
    size_t capacity_{0};
    size_t alignment_{4096};
};

} // namespace aegon::storage

