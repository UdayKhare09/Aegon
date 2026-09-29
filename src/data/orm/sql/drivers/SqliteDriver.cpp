#include "SqliteDriver.h"
#include <sqlite3.h>
#include <stdexcept>
#include <utility>

namespace aegon::data::orm::sql::drivers {

int sqlite_bind_param(sqlite3_stmt* stmt, int slot, const SqlParam& p) {
    switch (p.kind) {
        case SqlParam::Kind::Null:
            return sqlite3_bind_null(stmt, slot);
        case SqlParam::Kind::Text:
            return sqlite3_bind_text(stmt, slot,
                                     p.text_view.data(),
                                     static_cast<int>(p.text_view.size()),
                                     SQLITE_TRANSIENT);
        case SqlParam::Kind::Int64:
            return sqlite3_bind_int64(stmt, slot, p.i64);
        case SqlParam::Kind::Double:
            return sqlite3_bind_double(stmt, slot, p.f64);
        case SqlParam::Kind::Bool:
            return sqlite3_bind_int(stmt, slot, p.b ? 1 : 0);
        case SqlParam::Kind::Bytes:
            return sqlite3_bind_blob(stmt, slot,
                                     p.text_view.data(),
                                     static_cast<int>(p.text_view.size()),
                                     SQLITE_TRANSIENT);
    }
    return sqlite3_bind_null(stmt, slot);
}

SqliteConnection::SqliteConnection(const std::string& path) {
    std::string open_path = path;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    if (open_path == ":memory:" || open_path.starts_with("file:")) {
        flags |= SQLITE_OPEN_URI;
        if (open_path == ":memory:") {
            open_path = "file:aegon_memdb?mode=memory&cache=shared";
        }
    }
    int rc = sqlite3_open_v2(open_path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK || !db_) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "Failed to open SQLite database";
        if (db_) sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("SqliteConnection: " + err);
    }

    char* err_msg = nullptr;
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &err_msg);
    if (err_msg) sqlite3_free(err_msg);
}

SqliteConnection::~SqliteConnection() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

SqliteConnection::SqliteConnection(SqliteConnection&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)) {}

SqliteConnection& SqliteConnection::operator=(SqliteConnection&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close_v2(db_);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

core::Task<size_t> SqliteConnection::execute(std::string_view sql, std::span<const SqlParam> params) {
    if (!db_) throw std::runtime_error("SqliteConnection: database is closed.");

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("SqliteConnection execute prepare error: " +
                                 std::string(sqlite3_errmsg(db_)) + " in SQL: " + std::string(sql));
    }

    for (size_t i = 0; i < params.size(); ++i) {
        int brc = sqlite_bind_param(stmt, static_cast<int>(i + 1), params[i]);
        if (brc != SQLITE_OK) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            throw std::runtime_error("SqliteConnection bind error at param " +
                                     std::to_string(i + 1) + ": " + err);
        }
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("SqliteConnection execute step error: " + err);
    }

    size_t affected = static_cast<size_t>(sqlite3_changes(db_));
    sqlite3_finalize(stmt);
    co_return affected;
}

core::Task<std::vector<DriverRowView>> SqliteConnection::query(std::string_view sql, std::span<const SqlParam> params) {
    if (!db_) throw std::runtime_error("SqliteConnection: database is closed.");

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("SqliteConnection query prepare error: " +
                                 std::string(sqlite3_errmsg(db_)) + " in SQL: " + std::string(sql));
    }

    for (size_t i = 0; i < params.size(); ++i) {
        int brc = sqlite_bind_param(stmt, static_cast<int>(i + 1), params[i]);
        if (brc != SQLITE_OK) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            throw std::runtime_error("SqliteConnection bind error at param " +
                                     std::to_string(i + 1) + ": " + err);
        }
    }

    std::vector<DriverRowView> rows;
    int cols = sqlite3_column_count(stmt);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        DriverRowView row;
        for (int i = 0; i < cols; ++i) {
            if (sqlite3_column_type(stmt, i) == SQLITE_NULL) {
                row.add_null();
            } else {
                const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                row.add_value(txt ? std::string(txt) : "");
            }
        }
        rows.push_back(std::move(row));
    }

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("SqliteConnection query step error: " + err + " in SQL: " + std::string(sql));
    }

    sqlite3_finalize(stmt);
    co_return rows;
}

core::Task<void> SqliteConnection::begin_transaction() {
    co_await execute("BEGIN TRANSACTION;", {});
}

core::Task<void> SqliteConnection::commit_transaction() {
    co_await execute("COMMIT;", {});
}

core::Task<void> SqliteConnection::rollback_transaction() {
    co_await execute("ROLLBACK;", {});
}

DatabaseDialect SqliteConnection::dialect() const noexcept {
    return DatabaseDialect::SQLite;
}

bool SqliteConnection::is_valid() const noexcept {
    return db_ != nullptr;
}

sqlite3* SqliteConnection::raw_handle() const noexcept {
    return db_;
}

std::unique_ptr<PerCoreConnectionPool> create_sqlite_pool(std::string path, size_t max_idle) {
    return std::make_unique<PerCoreConnectionPool>(
        [path = std::move(path)]() -> std::unique_ptr<Connection> {
            return std::make_unique<SqliteConnection>(path);
        },
        max_idle
    );
}

} // namespace aegon::data::orm::sql::drivers
