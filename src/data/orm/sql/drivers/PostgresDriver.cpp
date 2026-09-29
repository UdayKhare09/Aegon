#include "PostgresDriver.h"
#include <libpq-fe.h>
#include <poll.h>
#include <stdexcept>
#include <utility>
#include <cstdlib>

namespace aegon::data::orm::sql::drivers {

std::string_view pg_raw_getter(void* handle, int row, int col) {
    auto* res = static_cast<PGresult*>(handle);
    const char* val = PQgetvalue(res, row, col);
    int len = PQgetlength(res, row, col);
    return std::string_view(val, len);
}

bool pg_is_null(void* handle, int row, int col) {
    return PQgetisnull(static_cast<PGresult*>(handle), row, col) != 0;
}

struct PgParamArrays {
    std::vector<std::string> text_bufs;
    std::vector<const char*> paramValues;
    std::vector<int>         paramLengths;
    std::vector<int>         paramFormats; // 0 = text, 1 = binary

    void build(std::span<const SqlParam> params) {
        const size_t n = params.size();
        text_bufs.resize(n);
        paramValues.resize(n);
        paramLengths.resize(n);
        paramFormats.assign(n, 0);

        for (size_t i = 0; i < n; ++i) {
            const auto& p = params[i];
            switch (p.kind) {
                case SqlParam::Kind::Null:
                    paramValues[i]  = nullptr;
                    paramLengths[i] = 0;
                    break;

                case SqlParam::Kind::Text:
                    paramValues[i]  = p.text_view.data();
                    paramLengths[i] = static_cast<int>(p.text_view.size());
                    break;

                case SqlParam::Kind::Int64:
                    text_bufs[i]    = std::to_string(p.i64);
                    paramValues[i]  = text_bufs[i].c_str();
                    paramLengths[i] = static_cast<int>(text_bufs[i].size());
                    break;

                case SqlParam::Kind::Double:
                    text_bufs[i]    = std::to_string(p.f64);
                    paramValues[i]  = text_bufs[i].c_str();
                    paramLengths[i] = static_cast<int>(text_bufs[i].size());
                    break;

                case SqlParam::Kind::Bool:
                    paramValues[i]  = p.b ? "true" : "false";
                    paramLengths[i] = p.b ? 4 : 5;
                    break;

                case SqlParam::Kind::Bytes:
                    paramValues[i]  = p.text_view.data();
                    paramLengths[i] = static_cast<int>(p.text_view.size());
                    paramFormats[i] = 1;
                    break;
            }
        }
    }
};

struct PostgresConnection::PipelineAwaiter {
    PostgresConnection& conn;
    PGresult** out_res;
    AwaitKind kind;

    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        conn.in_flight_.push_back({h, out_res, kind});
    }
    void await_resume() noexcept {}
};

core::Task<void> PostgresConnection::run_reader(core::EventLoop& loop) {
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

void PostgresConnection::ensure_reader() {
    if (pipelined_ && !reader_running_ && conn_) {
        auto* loop = core::EventLoop::current();
        if (loop) {
            reader_running_ = true;
            loop->spawn(run_reader(*loop));
        }
    }
}

PostgresConnection::PostgresConnection(const std::string& conninfo, bool pipelined,
                                       std::vector<PreparedStatementDef> prepared_stmts)
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

PostgresConnection::~PostgresConnection() {
    if (conn_) {
        if (pipelined_) {
            PQexitPipelineMode(conn_);
        }
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

PostgresConnection::PostgresConnection(PostgresConnection&& other) noexcept
    : conn_(std::exchange(other.conn_, nullptr)),
      pipelined_(other.pipelined_),
      fd_(other.fd_),
      reader_running_(other.reader_running_),
      prepared_defs_(std::move(other.prepared_defs_)),
      prepared_map_(std::move(other.prepared_map_)) {}

PostgresConnection& PostgresConnection::operator=(PostgresConnection&& other) noexcept {
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

core::Task<size_t> PostgresConnection::execute(std::string_view sql, std::span<const SqlParam> params) {
    if (!conn_) throw std::runtime_error("PostgresConnection: connection is closed.");

    PgParamArrays pg;
    pg.build(params);

    PGresult* res = nullptr;
    auto it = prepared_map_.find(sql);
    bool is_prepared = (it != prepared_map_.end());

    if (pipelined_) {
        ensure_reader();

        int sent = 0;
        if (is_prepared) {
            sent = PQsendQueryPrepared(conn_, it->second.c_str(),
                                       static_cast<int>(params.size()),
                                       pg.paramValues.data(),
                                       pg.paramLengths.data(),
                                       pg.paramFormats.data(), 0);
        } else {
            std::string sql_str(sql);
            sent = PQsendQueryParams(conn_, sql_str.c_str(),
                                     static_cast<int>(params.size()),
                                     nullptr,
                                     pg.paramValues.data(),
                                     pg.paramLengths.data(),
                                     pg.paramFormats.data(), 0);
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
            res = PQexecPrepared(conn_, it->second.c_str(),
                                 static_cast<int>(params.size()),
                                 pg.paramValues.data(),
                                 pg.paramLengths.data(),
                                 pg.paramFormats.data(), 0);
        } else {
            std::string sql_str(sql);
            res = PQexecParams(conn_, sql_str.c_str(),
                               static_cast<int>(params.size()),
                               nullptr,
                               pg.paramValues.data(),
                               pg.paramLengths.data(),
                               pg.paramFormats.data(), 0);
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
        std::string_view sv(cmd_status);
        size_t last_space = sv.rfind(' ');
        if (last_space != std::string_view::npos) {
            affected = static_cast<size_t>(std::strtoull(cmd_status + last_space + 1, nullptr, 10));
        }
    }

    PQclear(res);
    co_return affected;
}

core::Task<std::vector<DriverRowView>> PostgresConnection::query(std::string_view sql, std::span<const SqlParam> params) {
    if (!conn_) throw std::runtime_error("PostgresConnection: connection is closed.");

    PgParamArrays pg;
    pg.build(params);

    PGresult* res = nullptr;
    auto it = prepared_map_.find(sql);
    bool is_prepared = (it != prepared_map_.end());

    if (pipelined_) {
        ensure_reader();

        int sent = 0;
        if (is_prepared) {
            sent = PQsendQueryPrepared(conn_, it->second.c_str(),
                                       static_cast<int>(params.size()),
                                       pg.paramValues.data(),
                                       pg.paramLengths.data(),
                                       pg.paramFormats.data(), 0);
        } else {
            std::string sql_str(sql);
            sent = PQsendQueryParams(conn_, sql_str.c_str(),
                                     static_cast<int>(params.size()),
                                     nullptr,
                                     pg.paramValues.data(),
                                     pg.paramLengths.data(),
                                     pg.paramFormats.data(), 0);
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
            res = PQexecPrepared(conn_, it->second.c_str(),
                                 static_cast<int>(params.size()),
                                 pg.paramValues.data(),
                                 pg.paramLengths.data(),
                                 pg.paramFormats.data(), 0);
        } else {
            std::string sql_str(sql);
            res = PQexecParams(conn_, sql_str.c_str(),
                               static_cast<int>(params.size()),
                               nullptr,
                               pg.paramValues.data(),
                               pg.paramLengths.data(),
                               pg.paramFormats.data(), 0);
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
    std::vector<DriverRowView> rows;
    rows.reserve(rows_count);

    for (int r = 0; r < rows_count; ++r) {
        rows.emplace_back(r == 0 ? shared_res : nullptr, res, r, cols_count, &pg_raw_getter, &pg_is_null);
    }

    co_return rows;
}

core::Task<void> PostgresConnection::begin_transaction() {
    co_await execute("BEGIN;", {});
}

core::Task<void> PostgresConnection::commit_transaction() {
    co_await execute("COMMIT;", {});
}

core::Task<void> PostgresConnection::rollback_transaction() {
    co_await execute("ROLLBACK;", {});
}

DatabaseDialect PostgresConnection::dialect() const noexcept {
    return DatabaseDialect::PostgreSQL;
}

bool PostgresConnection::is_valid() const noexcept {
    return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
}

size_t PostgresConnection::in_flight_count() const noexcept {
    return in_flight_.size();
}

bool PostgresConnection::is_pipelined() const noexcept {
    return pipelined_;
}

PGconn* PostgresConnection::raw_handle() const noexcept {
    return conn_;
}

std::unique_ptr<PerCoreConnectionPool> create_postgres_pool(
    std::string conninfo,
    size_t pool_per_core,
    bool pipelined,
    std::vector<PreparedStatementDef> prepared_stmts
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
