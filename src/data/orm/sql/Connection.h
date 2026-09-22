#pragma once

#include "Dialect.h"
#include "RowView.h"
#include "QueryResult.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace aegon::data::orm::sql {

struct PreparedStatementDef {
    std::string name;
    std::string sql;
};

class Connection {
public:
    virtual ~Connection() = default;

    // Asynchronous query execution returning number of affected rows (for INSERT/UPDATE/DELETE)
    virtual core::Task<size_t> execute(std::string_view sql, const std::vector<std::string>& params) = 0;

    // Asynchronous query execution returning a collection of rows (for SELECT)
    virtual core::Task<std::vector<MockRowView>> query(std::string_view sql, const std::vector<std::string>& params) = 0;

    // Transaction lifecycle primitives
    virtual core::Task<void> begin_transaction() = 0;
    virtual core::Task<void> commit_transaction() = 0;
    virtual core::Task<void> rollback_transaction() = 0;

    [[nodiscard]] virtual DatabaseDialect dialect() const noexcept = 0;
    [[nodiscard]] virtual bool is_valid() const noexcept = 0;
    [[nodiscard]] virtual size_t in_flight_count() const noexcept { return 0; }
    [[nodiscard]] virtual bool is_pipelined() const noexcept { return false; }
};

} // namespace aegon::data::orm::sql
