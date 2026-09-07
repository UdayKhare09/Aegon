#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace aegon::data::orm::sql {

enum class DatabaseDialect : uint8_t {
    PostgreSQL,
    MySQL,
    SQLite
};

struct DialectTraits {
    [[nodiscard]] static constexpr std::string_view name(DatabaseDialect d) noexcept {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "PostgreSQL";
            case DatabaseDialect::MySQL:      return "MySQL";
            case DatabaseDialect::SQLite:     return "SQLite";
        }
        return "Unknown";
    }

    [[nodiscard]] static constexpr char quote_char(DatabaseDialect d) noexcept {
        return d == DatabaseDialect::MySQL ? '`' : '"';
    }

    static std::string quote_identifier(DatabaseDialect d, std::string_view id) {
        char q = quote_char(d);
        auto dot = id.find('.');
        if (dot != std::string_view::npos) {
            std::string s;
            s.reserve(id.size() + 4);
            s.push_back(q);
            s.append(id.substr(0, dot));
            s.push_back(q);
            s.push_back('.');
            s.push_back(q);
            s.append(id.substr(dot + 1));
            s.push_back(q);
            return s;
        }
        std::string s;
        s.reserve(id.size() + 2);
        s.push_back(q);
        s.append(id);
        s.push_back(q);
        return s;
    }

    [[nodiscard]] static constexpr std::string_view auto_increment_pk(DatabaseDialect d) noexcept {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY";
            case DatabaseDialect::MySQL:      return "BIGINT AUTO_INCREMENT PRIMARY KEY";
            case DatabaseDialect::SQLite:     return "INTEGER PRIMARY KEY AUTOINCREMENT";
        }
        return "INTEGER PRIMARY KEY";
    }

    [[nodiscard]] static constexpr std::string_view current_timestamp(DatabaseDialect d) noexcept {
        switch (d) {
            case DatabaseDialect::PostgreSQL: return "CURRENT_TIMESTAMP";
            case DatabaseDialect::MySQL:      return "CURRENT_TIMESTAMP(6)";
            case DatabaseDialect::SQLite:     return "CURRENT_TIMESTAMP";
        }
        return "CURRENT_TIMESTAMP";
    }

    [[nodiscard]] static constexpr bool supports_returning(DatabaseDialect d) noexcept {
        return d == DatabaseDialect::PostgreSQL || d == DatabaseDialect::SQLite;
    }

    static void format_placeholder(DatabaseDialect d, size_t index_1based, std::string& out) {
        if (d == DatabaseDialect::PostgreSQL) {
            out.push_back('$');
            out.append(std::to_string(index_1based));
        } else {
            out.push_back('?');
        }
    }
};

} // namespace aegon::data::orm::sql
