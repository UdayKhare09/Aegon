#include "Dialect.h"

namespace aegon::data::orm::sql {

std::string DialectTraits::quote_identifier(DatabaseDialect d, std::string_view id) {
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

namespace {
constexpr std::string_view kPgPlaceholders[] = {
    "$0",  "$1",  "$2",  "$3",  "$4",  "$5",  "$6",  "$7",  "$8",  "$9",
    "$10", "$11", "$12", "$13", "$14", "$15", "$16", "$17", "$18", "$19",
    "$20", "$21", "$22", "$23", "$24", "$25", "$26", "$27", "$28", "$29",
    "$30", "$31", "$32", "$33", "$34", "$35", "$36", "$37", "$38", "$39",
    "$40", "$41", "$42", "$43", "$44", "$45", "$46", "$47", "$48", "$49",
    "$50", "$51", "$52", "$53", "$54", "$55", "$56", "$57", "$58", "$59",
    "$60", "$61", "$62", "$63", "$64"
};
constexpr size_t kMaxCachedPlaceholders = sizeof(kPgPlaceholders) / sizeof(kPgPlaceholders[0]) - 1;
}

/**
 * @brief Append a placeholder to out: $N for PostgreSQL or ? for SQLite.
 * index_1based is the parameter position; it is ignored for SQLite and not validated.
 */
void DialectTraits::format_placeholder(DatabaseDialect d, size_t index_1based, std::string& out) {
    if (d == DatabaseDialect::PostgreSQL) {
        if (index_1based <= kMaxCachedPlaceholders) {
            out.append(kPgPlaceholders[index_1based]);
        } else {
            out.push_back('$');
            out.append(std::to_string(index_1based));
        }
    } else {
        out.push_back('?');
    }
}

} // namespace aegon::data::orm::sql
