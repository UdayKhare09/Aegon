#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace aegon::data::orm::sql {

enum class DatabaseDialect : uint8_t {
    PostgreSQL,
    SQLite
};

struct DialectTraits {
    [[nodiscard]] static constexpr std::string_view name(DatabaseDialect d) noexcept {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "PostgreSQL";
            case DatabaseDialect::SQLite:     return "SQLite";
        }
        return "Unknown";
    }

    [[nodiscard]] static constexpr char quote_char([[maybe_unused]] DatabaseDialect d) noexcept {
        return '"';
    }

    [[nodiscard]] static std::string quote_identifier(DatabaseDialect d, std::string_view id);

    [[nodiscard]] static constexpr std::string_view auto_increment_pk(DatabaseDialect d) noexcept {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY";
            case DatabaseDialect::SQLite:     return "INTEGER PRIMARY KEY AUTOINCREMENT";
        }
        return "INTEGER PRIMARY KEY";
    }

    [[nodiscard]] static constexpr std::string_view current_timestamp([[maybe_unused]] DatabaseDialect d) noexcept {
        return "CURRENT_TIMESTAMP";
    }

    [[nodiscard]] static constexpr bool supports_returning([[maybe_unused]] DatabaseDialect d) noexcept {
        return true;
    }

    static void format_placeholder(DatabaseDialect d, size_t index_1based, std::string& out);
};

} // namespace aegon::data::orm::sql
