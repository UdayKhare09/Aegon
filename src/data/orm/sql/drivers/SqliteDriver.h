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

namespace aegon::data::orm::sql::drivers {

class SqliteConnection : public Connection {
    sqlite3* db_{nullptr};

public:
    explicit SqliteConnection(const std::string& path = ":memory:") {
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

        // Enable foreign keys
        char* err_msg = nullptr;
        sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
    }

    ~SqliteConnection() override {
        if (db_) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
    }

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    SqliteConnection(SqliteConnection&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}
    SqliteConnection& operator=(SqliteConnection&& other) noexcept {
        if (this != &other) {
            if (db_) sqlite3_close_v2(db_);
            db_ = std::exchange(other.db_, nullptr);
        }
        return *this;
    }

    core::Task<size_t> execute(std::string_view sql, const std::vector<std::string>& params) override {
        if (!db_) throw std::runtime_error("SqliteConnection: database is closed.");

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("SqliteConnection execute prepare error: " + std::string(sqlite3_errmsg(db_)) + " in SQL: " + std::string(sql));
        }

        for (size_t i = 0; i < params.size(); ++i) {
            if (params[i] == SQL_NULL_SENTINEL) {
                sqlite3_bind_null(stmt, static_cast<int>(i + 1));
            } else {
                sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].data(), static_cast<int>(params[i].size()), SQLITE_TRANSIENT);
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

    core::Task<std::vector<MockRowView>> query(std::string_view sql, const std::vector<std::string>& params) override {
        if (!db_) throw std::runtime_error("SqliteConnection: database is closed.");

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("SqliteConnection query prepare error: " + std::string(sqlite3_errmsg(db_)) + " in SQL: " + std::string(sql));
        }

        for (size_t i = 0; i < params.size(); ++i) {
            if (params[i] == SQL_NULL_SENTINEL) {
                sqlite3_bind_null(stmt, static_cast<int>(i + 1));
            } else {
                sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].data(), static_cast<int>(params[i].size()), SQLITE_TRANSIENT);
            }
        }

        std::vector<MockRowView> rows;
        int cols = sqlite3_column_count(stmt);

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            MockRowView row;
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

    core::Task<void> begin_transaction() override {
        co_await execute("BEGIN TRANSACTION;", {});
    }

    core::Task<void> commit_transaction() override {
        co_await execute("COMMIT;", {});
    }

    core::Task<void> rollback_transaction() override {
        co_await execute("ROLLBACK;", {});
    }

    [[nodiscard]] DatabaseDialect dialect() const noexcept override {
        return DatabaseDialect::SQLite;
    }

    [[nodiscard]] bool is_valid() const noexcept override {
        return db_ != nullptr;
    }

    [[nodiscard]] sqlite3* raw_handle() const noexcept {
        return db_;
    }
};

inline std::unique_ptr<PerCoreConnectionPool> create_sqlite_pool(std::string path = ":memory:", size_t max_idle = 16) {
    return std::make_unique<PerCoreConnectionPool>(
        [path = std::move(path)]() -> std::unique_ptr<Connection> {
            return std::make_unique<SqliteConnection>(path);
        },
        max_idle
    );
}

} // namespace aegon::data::orm::sql::drivers
