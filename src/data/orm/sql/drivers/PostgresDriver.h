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
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <coroutine>

namespace aegon::data::orm::sql::drivers {

static inline std::string_view pg_raw_getter(void* handle, int row, int col) {
    auto* res = static_cast<PGresult*>(handle);
    const char* val = PQgetvalue(res, row, col);
    int len = PQgetlength(res, row, col);
    return std::string_view(val, len);
}

static inline bool pg_is_null(void* handle, int row, int col) {
    return PQgetisnull(static_cast<PGresult*>(handle), row, col) != 0;
}

class PostgresConnection : public Connection {
    PGconn* conn_{nullptr};
    bool pipelined_{false};
    int fd_{-1};

    enum class AwaitKind { Tuple, Command };
    struct Waiter {
        std::coroutine_handle<> handle;
        PGresult** out_res{nullptr};
        AwaitKind kind{AwaitKind::Tuple};
    };
    std::deque<Waiter> in_flight_;
    bool reader_running_{false};

    struct PipelineAwaiter {
        PostgresConnection& conn;
        PGresult** out_res;
        AwaitKind kind;

        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            conn.in_flight_.push_back({h, out_res, kind});
        }
        void await_resume() noexcept {}
    };

    core::Task<void> run_reader(core::EventLoop& loop) {
        while (conn_ && pipelined_) {
            (void)co_await loop.ring().poll(fd_, POLLIN);
            if (!conn_ || PQconsumeInput(conn_) == 0) {
                break;
            }

            while (conn_ && !PQisBusy(conn_)) {
                PGresult* r = PQgetResult(conn_);
                if (!r) break;
                ExecStatusType st = PQresultStatus(r);

                if (st == PGRES_PIPELINE_SYNC) {
                    PQclear(r);
                    continue;
                }

                if (!in_flight_.empty()) {
                    auto [h, out_res, _] = in_flight_.front();
                    in_flight_.pop_front();
                    *out_res = r;
                    h.resume();
                } else {
                    PQclear(r);
                }
            }
        }
        reader_running_ = false;
    }

    void ensure_reader() {
        if (pipelined_ && !reader_running_ && conn_) {
            auto* loop = core::EventLoop::current();
            if (loop) {
                reader_running_ = true;
                loop->spawn(run_reader(*loop));
            }
        }
    }


    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    std::vector<PreparedStatementDef> prepared_defs_;
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> prepared_map_;

public:
    explicit PostgresConnection(const std::string& conninfo, bool pipelined = true,
                                std::vector<PreparedStatementDef> prepared_stmts = {})
        : pipelined_(pipelined), prepared_defs_(std::move(prepared_stmts)) {
        conn_ = PQconnectdb(conninfo.c_str());
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
            std::string err = conn_ ? PQerrorMessage(conn_) : "Failed to allocate PGconn";
            if (conn_) PQfinish(conn_);
            conn_ = nullptr;
            throw std::runtime_error("PostgresConnection: " + err);
        }

        for (const auto& def : prepared_defs_) {
            PGresult* prep = PQexec(conn_, ("PREPARE " + def.name + " AS " + def.sql).c_str());
            if (prep) {
                PQclear(prep);
            }
            prepared_map_[def.sql] = def.name;
        }

        PQsetnonblocking(conn_, 1);
        fd_ = PQsocket(conn_);

        if (pipelined_ && core::EventLoop::current() != nullptr) {
            if (PQenterPipelineMode(conn_) != 1) {
                pipelined_ = false;
            }
        } else {
            pipelined_ = false;
        }
    }

    ~PostgresConnection() override {
        if (conn_) {
            if (pipelined_) {
                PQexitPipelineMode(conn_);
            }
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

    PostgresConnection(const PostgresConnection&) = delete;
    PostgresConnection& operator=(const PostgresConnection&) = delete;

    PostgresConnection(PostgresConnection&& other) noexcept
        : conn_(std::exchange(other.conn_, nullptr)),
          pipelined_(other.pipelined_),
          fd_(other.fd_),
          reader_running_(other.reader_running_),
          prepared_defs_(std::move(other.prepared_defs_)),
          prepared_map_(std::move(other.prepared_map_)) {}

    PostgresConnection& operator=(PostgresConnection&& other) noexcept {
        if (this != &other) {
            if (conn_) {
                if (pipelined_) PQexitPipelineMode(conn_);
                PQfinish(conn_);
            }
            conn_ = std::exchange(other.conn_, nullptr);
            pipelined_ = other.pipelined_;
            fd_ = other.fd_;
            reader_running_ = other.reader_running_;
            prepared_defs_ = std::move(other.prepared_defs_);
            prepared_map_ = std::move(other.prepared_map_);
        }
        return *this;
    }

    core::Task<size_t> execute(std::string_view sql, const std::vector<std::string>& params) override {
        if (!conn_) throw std::runtime_error("PostgresConnection: connection is closed.");

        std::array<const char*, 16> small_ptrs;
        std::vector<const char*> large_ptrs;
        const char** param_ptrs = nullptr;
        if (params.size() <= 16) {
            for (size_t i = 0; i < params.size(); ++i) {
                small_ptrs[i] = (params[i] == SQL_NULL_SENTINEL) ? nullptr : params[i].c_str();
            }
            param_ptrs = small_ptrs.data();
        } else {
            large_ptrs.reserve(params.size());
            for (const auto& p : params) {
                large_ptrs.push_back(p == SQL_NULL_SENTINEL ? nullptr : p.c_str());
            }
            param_ptrs = large_ptrs.data();
        }

        PGresult* res = nullptr;
        auto it = prepared_map_.find(sql);
        bool is_prepared = (it != prepared_map_.end());

        if (pipelined_) {
            ensure_reader();

            int sent = 0;
            if (is_prepared) {
                sent = PQsendQueryPrepared(conn_, it->second.c_str(), static_cast<int>(params.size()),
                                           param_ptrs, nullptr, nullptr, 0);
            } else {
                std::string sql_str(sql);
                sent = PQsendQueryParams(conn_, sql_str.c_str(), static_cast<int>(params.size()),
                                         nullptr, param_ptrs, nullptr, nullptr, 0);
            }
            if (!sent) {
                throw std::runtime_error("PostgresConnection send error: " + std::string(PQerrorMessage(conn_)));
            }
            PQpipelineSync(conn_);

            auto* loop = core::EventLoop::current();
            if (loop) {
                while (true) {
                    int flush_res = PQflush(conn_);
                    if (flush_res <= 0) break;
                    (void)co_await loop->ring().poll(fd_, POLLOUT);
                }
            }

            co_await PipelineAwaiter{*this, &res, AwaitKind::Command};
        } else {
            if (is_prepared) {
                res = PQexecPrepared(conn_, it->second.c_str(), static_cast<int>(params.size()),
                                     param_ptrs, nullptr, nullptr, 0);
            } else {
                std::string sql_str(sql);
                res = PQexecParams(conn_, sql_str.c_str(), static_cast<int>(params.size()),
                                   nullptr, param_ptrs, nullptr, nullptr, 0);
            }
        }

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
        const char* cmd_status = PQcmdStatus(res);
        if (cmd_tuples && cmd_tuples[0] != '\0') {
            affected = static_cast<size_t>(std::strtoull(cmd_tuples, nullptr, 10));
        } else if (cmd_status && cmd_status[0] != '\0') {
            // E.g. "UPDATE 1", "INSERT 0 1", "DELETE 1"
            std::string_view sv(cmd_status);
            size_t last_space = sv.rfind(' ');
            if (last_space != std::string_view::npos) {
                affected = static_cast<size_t>(std::strtoull(cmd_status + last_space + 1, nullptr, 10));
            }
        }

        PQclear(res);
        co_return affected;
    }

    core::Task<std::vector<MockRowView>> query(std::string_view sql, const std::vector<std::string>& params) override {
        if (!conn_) throw std::runtime_error("PostgresConnection: connection is closed.");

        std::array<const char*, 16> small_ptrs;
        std::vector<const char*> large_ptrs;
        const char** param_ptrs = nullptr;
        if (params.size() <= 16) {
            for (size_t i = 0; i < params.size(); ++i) {
                small_ptrs[i] = (params[i] == SQL_NULL_SENTINEL) ? nullptr : params[i].c_str();
            }
            param_ptrs = small_ptrs.data();
        } else {
            large_ptrs.reserve(params.size());
            for (const auto& p : params) {
                large_ptrs.push_back(p == SQL_NULL_SENTINEL ? nullptr : p.c_str());
            }
            param_ptrs = large_ptrs.data();
        }

        PGresult* res = nullptr;
        auto it = prepared_map_.find(sql);
        bool is_prepared = (it != prepared_map_.end());

        if (pipelined_) {
            ensure_reader();

            int sent = 0;
            if (is_prepared) {
                sent = PQsendQueryPrepared(conn_, it->second.c_str(), static_cast<int>(params.size()),
                                           param_ptrs, nullptr, nullptr, 0);
            } else {
                std::string sql_str(sql);
                sent = PQsendQueryParams(conn_, sql_str.c_str(), static_cast<int>(params.size()),
                                         nullptr, param_ptrs, nullptr, nullptr, 0);
            }
            if (!sent) {
                throw std::runtime_error("PostgresConnection send error: " + std::string(PQerrorMessage(conn_)));
            }
            PQpipelineSync(conn_);

            auto* loop = core::EventLoop::current();
            if (loop) {
                while (true) {
                    int flush_res = PQflush(conn_);
                    if (flush_res <= 0) break;
                    (void)co_await loop->ring().poll(fd_, POLLOUT);
                }
            }

            co_await PipelineAwaiter{*this, &res, AwaitKind::Tuple};
        } else {
            if (is_prepared) {
                res = PQexecPrepared(conn_, it->second.c_str(), static_cast<int>(params.size()),
                                     param_ptrs, nullptr, nullptr, 0);
            } else {
                std::string sql_str(sql);
                res = PQexecParams(conn_, sql_str.c_str(), static_cast<int>(params.size()),
                                   nullptr, param_ptrs, nullptr, nullptr, 0);
            }
        }

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
        auto shared_res = std::shared_ptr<PGresult>(res, PQclear);
        std::vector<MockRowView> rows;
        rows.reserve(rows_count);

        for (int r = 0; r < rows_count; ++r) {
            rows.emplace_back(r == 0 ? shared_res : nullptr, res, r, cols_count, &pg_raw_getter, &pg_is_null);
        }

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

    [[nodiscard]] size_t in_flight_count() const noexcept override {
        return in_flight_.size();
    }

    [[nodiscard]] bool is_pipelined() const noexcept override {
        return pipelined_;
    }

    [[nodiscard]] PGconn* raw_handle() const noexcept {
        return conn_;
    }
};

inline std::unique_ptr<PerCoreConnectionPool> create_postgres_pool(
    std::string conninfo,
    size_t pool_per_core = 8,
    bool pipelined = true,
    std::vector<PreparedStatementDef> prepared_stmts = {}
) {
    return std::make_unique<PerCoreConnectionPool>(
        [conninfo, pipelined, prepared_stmts = std::move(prepared_stmts)]() -> std::unique_ptr<Connection> {
            return std::make_unique<PostgresConnection>(conninfo, pipelined, prepared_stmts);
        },
        pool_per_core,
        pipelined
    );
}

} // namespace aegon::data::orm::sql::drivers
