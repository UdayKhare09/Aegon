#pragma once

#include "SqlParam.h"
#include <string>
#include <vector>

namespace aegon::data::orm::sql {

struct QueryResult {
    std::string sql;
    std::vector<SqlParam> params;
};

} // namespace aegon::data::orm::sql
