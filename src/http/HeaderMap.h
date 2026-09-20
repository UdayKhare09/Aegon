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

    HeaderMap() = default;

    void add(std::string_view name, std::string_view value) {
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
        add(name, value);
    }

    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const noexcept {
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
        return get(name).has_value();
    }

    [[nodiscard]] size_t size() const noexcept {
        return size_ + heap_headers_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    void clear() noexcept {
        size_ = 0;
        heap_headers_.clear();
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
    std::array<HeaderEntry, INLINE_CAPACITY> inline_headers_{};
    size_t size_{0};
    std::vector<HeaderEntry> heap_headers_{};
};

} // namespace aegon::http
