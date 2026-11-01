#pragma once

#include "core/simd/SimdString.h"
#include <string_view>
#include <vector>
#include <array>
#include <optional>
#include <algorithm>
#include <cctype>

namespace aegon::http {

/**
 * @brief Case-insensitive ASCII comparison for HTTP header names.
 */
inline bool iequals(std::string_view a, std::string_view b) noexcept {
    return core::simd::SimdString::iequals(a, b);
}

struct HeaderEntry {
    std::string_view name;
    std::string_view value;
};

/**
 * @brief Zero-allocation small-vector HTTP header map.
 *
 * Holds up to 32 headers inline without heap allocations, covering 99.9% of
 * real-world HTTP/1.1, HTTP/2, and HTTP/3 requests.
 */
class HeaderMap {
public:
    static constexpr size_t INLINE_CAPACITY = 32;

    enum Flags : uint8_t {
        FLAG_NONE              = 0,
        FLAG_CONTENT_LENGTH    = 1 << 0,
        FLAG_CONTENT_TYPE      = 1 << 1,
        FLAG_DATE              = 1 << 2,
        FLAG_TRANSFER_ENCODING = 1 << 3,
    };

    HeaderMap() = default;

    void add(std::string_view name, std::string_view value) {
        update_flags(name);
        if (size_ < INLINE_CAPACITY) {
            inline_headers_[size_++] = {name, value};
        } else {
            if (heap_headers_.empty()) {
                heap_headers_.reserve(16);
            }
            heap_headers_.push_back({name, value});
        }
    }

    void set(std::string_view name, std::string_view value) {
        update_flags(name);
        // Look in inline headers first
        for (size_t i = 0; i < size_; ++i) {
            if (iequals(inline_headers_[i].name, name)) {
                inline_headers_[i].value = value;
                return;
            }
        }
        // Look in heap headers
        for (auto& h : heap_headers_) {
            if (iequals(h.name, name)) {
                h.value = value;
                return;
            }
        }
        if (size_ < INLINE_CAPACITY) {
            inline_headers_[size_++] = {name, value};
        } else {
            if (heap_headers_.empty()) {
                heap_headers_.reserve(16);
            }
            heap_headers_.push_back({name, value});
        }
    }

    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const noexcept {
        if (name.size() == 14 && iequals(name, "Content-Length") && !has_content_length()) return std::nullopt;
        if (name.size() == 4 && iequals(name, "Date") && !has_date()) return std::nullopt;
        if (name.size() == 17 && iequals(name, "Transfer-Encoding") && !has_transfer_encoding()) return std::nullopt;
        if (name.size() == 12 && iequals(name, "Content-Type") && !has_content_type()) return std::nullopt;

        for (size_t i = 0; i < size_; ++i) {
            if (iequals(inline_headers_[i].name, name)) {
                return inline_headers_[i].value;
            }
        }
        for (const auto& h : heap_headers_) {
            if (iequals(h.name, name)) {
                return h.value;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool contains(std::string_view name) const noexcept {
        if (name.size() == 14 && iequals(name, "Content-Length")) return has_content_length();
        if (name.size() == 4 && iequals(name, "Date")) return has_date();
        if (name.size() == 17 && iequals(name, "Transfer-Encoding")) return has_transfer_encoding();
        if (name.size() == 12 && iequals(name, "Content-Type")) return has_content_type();
        return get(name).has_value();
    }

    [[nodiscard]] bool has_content_length() const noexcept { return (flags_ & FLAG_CONTENT_LENGTH) != 0; }
    [[nodiscard]] bool has_content_type() const noexcept { return (flags_ & FLAG_CONTENT_TYPE) != 0; }
    [[nodiscard]] bool has_date() const noexcept { return (flags_ & FLAG_DATE) != 0; }
    [[nodiscard]] bool has_transfer_encoding() const noexcept { return (flags_ & FLAG_TRANSFER_ENCODING) != 0; }

    [[nodiscard]] size_t size() const noexcept {
        return size_ + heap_headers_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    bool remove(std::string_view name) noexcept {
        bool removed = false;
        for (size_t i = 0; i < size_; ) {
            if (iequals(inline_headers_[i].name, name)) {
                for (size_t j = i; j + 1 < size_; ++j) {
                    inline_headers_[j] = inline_headers_[j + 1];
                }
                --size_;
                removed = true;
            } else {
                ++i;
            }
        }
        for (auto it = heap_headers_.begin(); it != heap_headers_.end(); ) {
            if (iequals(it->name, name)) {
                it = heap_headers_.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }
        if (removed) {
            recompute_flags();
        }
        return removed;
    }

    bool erase(std::string_view name) noexcept {
        return remove(name);
    }

    void clear() noexcept {
        size_ = 0;
        heap_headers_.clear();
        flags_ = FLAG_NONE;
    }

    // Iterator support
    struct ConstIterator {
        const HeaderMap& map;
        size_t index;

        bool operator==(const ConstIterator& other) const noexcept {
            return index == other.index;
        }

        bool operator!=(const ConstIterator& other) const noexcept {
            return index != other.index;
        }

        ConstIterator& operator++() noexcept {
            ++index;
            return *this;
        }

        const HeaderEntry& operator*() const noexcept {
            if (index < map.size_) {
                return map.inline_headers_[index];
            }
            return map.heap_headers_[index - map.size_];
        }

        const HeaderEntry* operator->() const noexcept {
            return &(**this);
        }
    };

    [[nodiscard]] ConstIterator begin() const noexcept { return ConstIterator{*this, 0}; }
    [[nodiscard]] ConstIterator end() const noexcept { return ConstIterator{*this, size()}; }

private:
    void update_flags(std::string_view name) noexcept {
        if (name.size() == 14 && iequals(name, "Content-Length")) {
            flags_ |= FLAG_CONTENT_LENGTH;
        } else if (name.size() == 12 && iequals(name, "Content-Type")) {
            flags_ |= FLAG_CONTENT_TYPE;
        } else if (name.size() == 4 && iequals(name, "Date")) {
            flags_ |= FLAG_DATE;
        } else if (name.size() == 17 && iequals(name, "Transfer-Encoding")) {
            flags_ |= FLAG_TRANSFER_ENCODING;
        }
    }

    void recompute_flags() noexcept {
        flags_ = FLAG_NONE;
        for (size_t i = 0; i < size_; ++i) {
            update_flags(inline_headers_[i].name);
        }
        for (const auto& h : heap_headers_) {
            update_flags(h.name);
        }
    }

    std::array<HeaderEntry, INLINE_CAPACITY> inline_headers_{};
    size_t size_{0};
    std::vector<HeaderEntry> heap_headers_{};
    uint8_t flags_{FLAG_NONE};
};

} // namespace aegon::http
