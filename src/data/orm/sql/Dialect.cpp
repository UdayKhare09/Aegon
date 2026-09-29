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

void DialectTraits::format_placeholder(DatabaseDialect d, size_t index_1based, std::string& out) {
    if (d == DatabaseDialect::PostgreSQL) {
        out.push_back('$');
        out.append(std::to_string(index_1based));
    } else {
        out.push_back('?');
    }
}

} // namespace aegon::data::orm::sql
