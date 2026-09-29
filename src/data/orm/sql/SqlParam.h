#pragma once

#include "data/types/Types.h"
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace aegon::data::orm::sql {

// ────────────────────────────────────────────────────────────────────────────
// SqlParam  —  a typed, low-allocation SQL parameter value.
//
// Design goals:
//   • No intermediate std::to_string / format_param_value strings for numerics.
//   • Text values borrow from caller memory via string_view where possible.
//   • Only opaque temporaries (UUID, DateTime, Blob …) own their storage.
//   • Drivers can dispatch on Kind to call typed bind APIs directly.
// ────────────────────────────────────────────────────────────────────────────

struct SqlParam {
    enum class Kind : uint8_t {
        Null,
        Text,    // string / string_view / char*
        Int64,   // any signed/unsigned integer ≤ 64-bit
        Double,  // float or double
        Bool,    // boolean (mapped to 0/1 for most drivers)
        Bytes,   // binary blob (BYTEA / BLOB)
    };

    Kind kind{Kind::Null};

    // Storage for values that must own their bytes (UUIDs, datetimes, blobs …)
    std::string storage;
    bool owns_storage{false};

    // Zero-copy view into `storage` or a caller-owned string.
    std::string_view text_view;

    // Numeric payload — avoids heap allocation entirely.
    union {
        int64_t  i64{0};
        double   f64;
        bool     b;
    };

    // ─── Factory functions ───────────────────────────────────────────────────

    [[nodiscard]] static SqlParam null() noexcept;
    [[nodiscard]] static SqlParam text(std::string_view sv) noexcept;
    [[nodiscard]] static SqlParam text_owned(std::string s) noexcept;
    [[nodiscard]] static SqlParam integer(int64_t v) noexcept;
    [[nodiscard]] static SqlParam real(double v) noexcept;
    [[nodiscard]] static SqlParam boolean(bool v) noexcept;
    [[nodiscard]] static SqlParam bytes(std::span<const std::byte> data) noexcept;

    // ─── Helpers ─────────────────────────────────────────────────────────────

    [[nodiscard]] bool is_null() const noexcept { return kind == Kind::Null; }
    [[nodiscard]] std::string to_debug_string() const;

    // ─── Copy / move ─────────────────────────────────────────────────────────

    SqlParam() = default;
    SqlParam(const SqlParam& o);
    SqlParam& operator=(const SqlParam& o);
    SqlParam(SqlParam&& o) noexcept;
    SqlParam& operator=(SqlParam&& o) noexcept;

    // ─── Comparison helpers ──────────────────────────────────────────────────
    [[nodiscard]] bool operator==(std::string_view sv) const noexcept;
    [[nodiscard]] bool operator==(const std::string& s) const noexcept;
    [[nodiscard]] bool operator==(const char* s) const noexcept;

// ────────────────────────────────────────────────────────────────────────────
// make_sql_param<T>  —  primary dispatch for building SqlParam from any C++ type
// ────────────────────────────────────────────────────────────────────────────

}; // struct SqlParam

template <typename T>
[[nodiscard]] inline SqlParam make_sql_param(const T& val) {
    using D = std::decay_t<T>;

    // --- std::optional ---
    if constexpr (requires { val.has_value(); }) {
        if (!val.has_value()) return SqlParam::null();
        return make_sql_param(*val);
    }
    // --- nullptr_t / monostate → NULL ---
    else if constexpr (std::is_null_pointer_v<D>) {
        return SqlParam::null();
    }
    // --- bool ---
    else if constexpr (std::is_same_v<D, bool>) {
        return SqlParam::boolean(val);
    }
    // --- signed integers ---
    else if constexpr (std::is_integral_v<D> && std::is_signed_v<D>) {
        return SqlParam::integer(static_cast<int64_t>(val));
    }
    // --- unsigned integers (cast to int64; uint64 loses top bit but fits all practical IDs) ---
    else if constexpr (std::is_integral_v<D> && std::is_unsigned_v<D>) {
        return SqlParam::integer(static_cast<int64_t>(static_cast<uint64_t>(val)));
    }
    // --- floating point ---
    else if constexpr (std::is_floating_point_v<D>) {
        return SqlParam::real(static_cast<double>(val));
    }
    // --- string types (std::string, std::string_view, const char*, char[N]) ---
    else if constexpr (std::is_convertible_v<D, std::string_view>) {
        if constexpr (std::is_pointer_v<T>) {
            if (val == nullptr) return SqlParam::null();
        }
        return SqlParam::text_owned(std::string(std::string_view(val)));
    }
    // --- Aegon UUID ---
    else if constexpr (std::is_same_v<D, types::UUID>) {
        return SqlParam::text_owned(val.to_string());
    }
    // --- Aegon DateTime ---
    else if constexpr (std::is_same_v<D, types::DateTime>) {
        return SqlParam::text_owned(val.to_iso8601());
    }
    // --- Aegon Date ---
    else if constexpr (std::is_same_v<D, types::Date>) {
        return SqlParam::text_owned(val.to_string());
    }
    // --- Aegon Time ---
    else if constexpr (std::is_same_v<D, types::Time>) {
        return SqlParam::text_owned(val.to_string());
    }
    // --- Aegon Json ---
    else if constexpr (std::is_same_v<D, types::Json>) {
        return SqlParam::text_owned(val.str());
    }
    // --- Aegon Blob ---
    else if constexpr (std::is_same_v<D, types::Blob>) {
        return SqlParam::bytes(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(val.data()), val.size()));
    }
    // --- Aegon Hash256 ---
    else if constexpr (std::is_same_v<D, types::Hash256>) {
        return SqlParam::bytes(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(val.data()), val.size()));
    }
    // --- Decimal (stored as text for precision) ---
    else if constexpr (requires { val.to_string(); }) {
        return SqlParam::text_owned(val.to_string());
    }
    // --- Fallback: null ---
    else {
        return SqlParam::null();
    }
}

} // namespace aegon::data::orm::sql
