#pragma once

#include "data/orm/sql/Connection.h"
#include "data/orm/sql/PerCoreConnectionPool.h"
#include "data/orm/sql/Expression.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <utility>

struct sqlite3;
struct sqlite3_stmt;

namespace aegon::data::orm::sql::drivers {

// Bind a single SqlParam to a sqlite3_stmt slot (1-indexed).
int sqlite_bind_param(sqlite3_stmt* stmt, int slot, const SqlParam& p);

class SqliteConnection : public Connection {
    sqlite3* db_{nullptr};

public:
    explicit SqliteConnection(const std::string& path = ":memory:");
    ~SqliteConnection() override;

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    SqliteConnection(SqliteConnection&& other) noexcept;
    SqliteConnection& operator=(SqliteConnection&& other) noexcept;

    core::Task<size_t> execute(std::string_view sql, std::span<const SqlParam> params) override;
    core::Task<std::vector<DriverRowView>> query(std::string_view sql, std::span<const SqlParam> params) override;

    core::Task<void> begin_transaction() override;
    core::Task<void> commit_transaction() override;
    core::Task<void> rollback_transaction() override;

    [[nodiscard]] DatabaseDialect dialect() const noexcept override;
    [[nodiscard]] bool is_valid() const noexcept override;
    [[nodiscard]] sqlite3* raw_handle() const noexcept;
};

std::unique_ptr<PerCoreConnectionPool> create_sqlite_pool(std::string path = ":memory:", size_t max_idle = 16);

} // namespace aegon::data::orm::sql::drivers
