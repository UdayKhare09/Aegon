#pragma once

#include <string>
#include <vector>

namespace aegon::data::orm::sql {

struct QueryResult {
    std::string sql;
    std::vector<std::string> params;
};

} // namespace aegon::data::orm::sql
