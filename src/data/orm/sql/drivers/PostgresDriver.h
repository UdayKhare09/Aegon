#pragma once

#include "data/orm/sql/Connection.h"
#include "data/orm/sql/PerCoreConnectionPool.h"
#include "data/orm/sql/Expression.h"
#include <libpq-fe.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <utility>
#include <cstdlib>

#include "core/EventLoop.h"
#include <poll.h>

namespace aegon::data::orm::sql::drivers {

class PostgresConnection : public Connection {
    PGconn* conn_{nullptr};

    core::Task<PGresult*> send_and_wait_result(std::string_view sql, const std::vector<const char*>& param_ptrs) {
        std::string sql_str(sql);
        int sent = PQsendQueryParams(conn_, sql_str.c_str(), static_cast<int>(param_ptrs.size()),
                                     nullptr, param_ptrs.data(), nullptr, nullptr, 0);
        if (!sent) {
            throw std::runtime_error("PostgresConnection send error: " + std::string(PQerrorMessage(conn_)));
        }

        auto* loop = core::EventLoop::current();
        int fd = PQsocket(conn_);

        if (loop) {
            while (true) {
                int flush_res = PQflush(conn_);
                if (flush_res <= 0) break;
                (void)co_await loop->ring().poll(fd, POLLOUT);
            }

            while (PQisBusy(conn_)) {
                (void)co_await loop->ring().poll(fd, POLLIN);
                if (PQconsumeInput(conn_) == 0) {
                    break;
                }
            }
        } else {
            while (PQflush(conn_) > 0) {
                struct pollfd pfd{fd, POLLOUT, 0};
                ::poll(&pfd, 1, 100);
            }
            while (PQisBusy(conn_)) {
                struct pollfd pfd{fd, POLLIN, 0};
                ::poll(&pfd, 1, 100);
                if (PQconsumeInput(conn_) == 0) break;
            }
        }

        PGresult* res = PQgetResult(conn_);
        while (PGresult* extra = PQgetResult(conn_)) {
            PQclear(extra);
        }
        co_return res;
    }

public:
    explicit PostgresConnection(const std::string& conninfo) {
        conn_ = PQconnectdb(conninfo.c_str());
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
            std::string err = conn_ ? PQerrorMessage(conn_) : "Failed to allocate PGconn";
            if (conn_) PQfinish(conn_);
            conn_ = nullptr;
            throw std::runtime_error("PostgresConnection: " + err);
        }
        PQsetnonblocking(conn_, 1);
    }

    ~PostgresConnection() override {
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

    PostgresConnection(const PostgresConnection&) = delete;
    PostgresConnection& operator=(const PostgresConnection&) = delete;

    PostgresConnection(PostgresConnection&& other) noexcept : conn_(std::exchange(other.conn_, nullptr)) {}
    PostgresConnection& operator=(PostgresConnection&& other) noexcept {
        if (this != &other) {
            if (conn_) PQfinish(conn_);
            conn_ = std::exchange(other.conn_, nullptr);
        }
        return *this;
    }

    core::Task<size_t> execute(std::string_view sql, const std::vector<std::string>& params) override {
        if (!conn_) throw std::runtime_error("PostgresConnection: connection is closed.");

        std::vector<const char*> param_ptrs;
        param_ptrs.reserve(params.size());
        for (const auto& p : params) {
            if (p == SQL_NULL_SENTINEL) {
                param_ptrs.push_back(nullptr);
            } else {
                param_ptrs.push_back(p.c_str());
            }
        }

        PGresult* res = co_await send_and_wait_result(sql, param_ptrs);
        if (!res) {
            throw std::runtime_error("PostgresConnection execute error: null result returned");
        }

        ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("PostgresConnection execute error: " + err + " in SQL: " + std::string(sql));
        }

        size_t affected = 0;
        const char* cmd_tuples = PQcmdTuples(res);
        if (cmd_tuples && cmd_tuples[0] != '\0') {
            affected = static_cast<size_t>(std::strtoull(cmd_tuples, nullptr, 10));
        }

        PQclear(res);
        co_return affected;
    }

    core::Task<std::vector<MockRowView>> query(std::string_view sql, const std::vector<std::string>& params) override {
        if (!conn_) throw std::runtime_error("PostgresConnection: connection is closed.");

        std::vector<const char*> param_ptrs;
        param_ptrs.reserve(params.size());
        for (const auto& p : params) {
            if (p == SQL_NULL_SENTINEL) {
                param_ptrs.push_back(nullptr);
            } else {
                param_ptrs.push_back(p.c_str());
            }
        }

        PGresult* res = co_await send_and_wait_result(sql, param_ptrs);
        if (!res) {
            throw std::runtime_error("PostgresConnection query error: null result returned");
        }

        ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_TUPLES_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("PostgresConnection query error: " + err + " in SQL: " + std::string(sql));
        }

        int rows_count = PQntuples(res);
        int cols_count = PQnfields(res);
        std::vector<MockRowView> rows;
        rows.reserve(rows_count);

        for (int r = 0; r < rows_count; ++r) {
            MockRowView row;
            for (int c = 0; c < cols_count; ++c) {
                if (PQgetisnull(res, r, c)) {
                    row.add_null();
                } else {
                    const char* val = PQgetvalue(res, r, c);
                    row.add_value(val ? std::string(val) : "");
                }
            }
            rows.push_back(std::move(row));
        }

        PQclear(res);
        co_return rows;
    }

    core::Task<void> begin_transaction() override {
        co_await execute("BEGIN;", {});
    }

    core::Task<void> commit_transaction() override {
        co_await execute("COMMIT;", {});
    }

    core::Task<void> rollback_transaction() override {
        co_await execute("ROLLBACK;", {});
    }

    [[nodiscard]] DatabaseDialect dialect() const noexcept override {
        return DatabaseDialect::PostgreSQL;
    }

    [[nodiscard]] bool is_valid() const noexcept override {
        return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
    }

    [[nodiscard]] PGconn* raw_handle() const noexcept {
        return conn_;
    }
};

inline std::unique_ptr<PerCoreConnectionPool> create_postgres_pool(std::string conninfo, size_t max_idle = 16) {
    return std::make_unique<PerCoreConnectionPool>(
        [conninfo = std::move(conninfo)]() -> std::unique_ptr<Connection> {
            return std::make_unique<PostgresConnection>(conninfo);
        },
        max_idle
    );
}

} // namespace aegon::data::orm::sql::drivers
