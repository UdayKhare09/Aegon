#pragma once

#include "Dialect.h"
#include "data/types/Types.h"
#include <string>
#include <string_view>
#include <optional>
#include <concepts>
#include <cstdint>

namespace aegon::data::orm::sql {

template <typename T>
struct TypeMapper;

// Helper concept
template <typename T>
concept HasTypeMapper = requires(DatabaseDialect d) {
    { TypeMapper<T>::column_type(d, 0) } -> std::same_as<std::string>;
    { TypeMapper<T>::is_nullable } -> std::convertible_to<bool>;
};

// -------------------------------------------------------------
// Primitives
// -------------------------------------------------------------

template <>
struct TypeMapper<bool> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "BOOLEAN";
            case DatabaseDialect::MySQL:      return "TINYINT(1)";
            case DatabaseDialect::SQLite:     return "INTEGER";
        }
        return "INTEGER";
    }
};

template <>
struct TypeMapper<int16_t> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "SMALLINT";
            case DatabaseDialect::MySQL:      return "SMALLINT";
            case DatabaseDialect::SQLite:     return "INTEGER";
        }
        return "INTEGER";
    }
};

template <>
struct TypeMapper<uint16_t> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "SMALLINT";
            case DatabaseDialect::MySQL:      return "SMALLINT UNSIGNED";
            case DatabaseDialect::SQLite:     return "INTEGER";
        }
        return "INTEGER";
    }
};

template <>
struct TypeMapper<int32_t> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "INTEGER";
            case DatabaseDialect::MySQL:      return "INT";
            case DatabaseDialect::SQLite:     return "INTEGER";
        }
        return "INTEGER";
    }
};

template <>
struct TypeMapper<uint32_t> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "BIGINT"; // Postgres has no unsigned int
            case DatabaseDialect::MySQL:      return "INT UNSIGNED";
            case DatabaseDialect::SQLite:     return "INTEGER";
        }
        return "INTEGER";
    }
};

template <>
struct TypeMapper<int64_t> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "BIGINT";
            case DatabaseDialect::MySQL:      return "BIGINT";
            case DatabaseDialect::SQLite:     return "INTEGER";
        }
        return "INTEGER";
    }
};

template <>
struct TypeMapper<uint64_t> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "NUMERIC(20, 0)"; // 64-bit unsigned fits in numeric
            case DatabaseDialect::MySQL:      return "BIGINT UNSIGNED";
            case DatabaseDialect::SQLite:     return "INTEGER";
        }
        return "INTEGER";
    }
};

template <>
struct TypeMapper<float> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "REAL";
            case DatabaseDialect::MySQL:      return "FLOAT";
            case DatabaseDialect::SQLite:     return "REAL";
        }
        return "REAL";
    }
};

template <>
struct TypeMapper<double> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "DOUBLE PRECISION";
            case DatabaseDialect::MySQL:      return "DOUBLE";
            case DatabaseDialect::SQLite:     return "REAL";
        }
        return "REAL";
    }
};

template <>
struct TypeMapper<std::string> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t length = 0) {
        if (length > 0) {
            return "VARCHAR(" + std::to_string(length) + ")";
        }
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "TEXT";
            case DatabaseDialect::MySQL:      return "TEXT";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

template <>
struct TypeMapper<std::string_view> : TypeMapper<std::string> {};

// -------------------------------------------------------------
// Aegon Foundational Data Types
// -------------------------------------------------------------

template <>
struct TypeMapper<types::UUID> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "UUID";
            case DatabaseDialect::MySQL:      return "BINARY(16)";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

template <>
struct TypeMapper<types::DateTime> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "TIMESTAMPTZ";
            case DatabaseDialect::MySQL:      return "DATETIME(6)";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

template <>
struct TypeMapper<types::Date> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "DATE";
            case DatabaseDialect::MySQL:      return "DATE";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

template <>
struct TypeMapper<types::Time> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "TIME";
            case DatabaseDialect::MySQL:      return "TIME(6)";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

template <uint8_t P, uint8_t S>
struct TypeMapper<types::Decimal<P, S>> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        std::string ps = "(" + std::to_string(P) + ", " + std::to_string(S) + ")";
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "NUMERIC" + ps;
            case DatabaseDialect::MySQL:      return "DECIMAL" + ps;
            case DatabaseDialect::SQLite:     return "NUMERIC";
        }
        return "NUMERIC";
    }
};

template <>
struct TypeMapper<types::Json> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "JSONB";
            case DatabaseDialect::MySQL:      return "JSON";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

template <>
struct TypeMapper<types::IpAddress> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "INET";
            case DatabaseDialect::MySQL:      return "VARBINARY(16)";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

template <>
struct TypeMapper<types::MacAddress> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "MACADDR";
            case DatabaseDialect::MySQL:      return "BINARY(6)";
            case DatabaseDialect::SQLite:     return "TEXT";
        }
        return "TEXT";
    }
};

template <>
struct TypeMapper<types::Blob> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "BYTEA";
            case DatabaseDialect::MySQL:      return "LONGBLOB";
            case DatabaseDialect::SQLite:     return "BLOB";
        }
        return "BLOB";
    }
};

template <>
struct TypeMapper<types::Hash256> {
    static constexpr bool is_nullable = false;
    static std::string column_type(DatabaseDialect d, size_t = 0) {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "BYTEA";
            case DatabaseDialect::MySQL:      return "BINARY(32)";
            case DatabaseDialect::SQLite:     return "BLOB";
        }
        return "BLOB";
    }
};

// -------------------------------------------------------------
// std::optional<T> Specialization (Nullable)
// -------------------------------------------------------------

template <typename T>
struct TypeMapper<std::optional<T>> {
    static constexpr bool is_nullable = true;
    static std::string column_type(DatabaseDialect d, size_t length = 0) {
        return TypeMapper<T>::column_type(d, length);
    }
};

} // namespace aegon::data::orm::sql
