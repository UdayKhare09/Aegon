#pragma once

#include "data/orm/sql/Connection.h"
#include "data/orm/sql/PerCoreConnectionPool.h"
#include "data/orm/sql/Expression.h"
#include "core/EventLoop.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <deque>
#include <coroutine>

struct pg_conn;
typedef struct pg_conn PGconn;
struct pg_result;
typedef struct pg_result PGresult;

namespace aegon::data::orm::sql::drivers {

std::string_view pg_raw_getter(void* handle, int row, int col);
bool pg_is_null(void* handle, int row, int col);

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

    struct PipelineAwaiter;
    core::Task<void> run_reader(core::EventLoop& loop);
    void ensure_reader();

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
                                std::vector<PreparedStatementDef> prepared_stmts = {});
    ~PostgresConnection() override;

    PostgresConnection(const PostgresConnection&) = delete;
    PostgresConnection& operator=(const PostgresConnection&) = delete;

    PostgresConnection(PostgresConnection&& other) noexcept;
    PostgresConnection& operator=(PostgresConnection&& other) noexcept;

    core::Task<size_t> execute(std::string_view sql, std::span<const SqlParam> params) override;
    core::Task<std::vector<DriverRowView>> query(std::string_view sql, std::span<const SqlParam> params) override;

    core::Task<void> begin_transaction() override;
    core::Task<void> commit_transaction() override;
    core::Task<void> rollback_transaction() override;

    [[nodiscard]] DatabaseDialect dialect() const noexcept override;
    [[nodiscard]] bool is_valid() const noexcept override;
    [[nodiscard]] size_t in_flight_count() const noexcept override;
    [[nodiscard]] bool is_pipelined() const noexcept override;
    [[nodiscard]] PGconn* raw_handle() const noexcept;
};

std::unique_ptr<PerCoreConnectionPool> create_postgres_pool(
    std::string conninfo,
    size_t pool_per_core = 8,
    bool pipelined = true,
    std::vector<PreparedStatementDef> prepared_stmts = {}
);

} // namespace aegon::data::orm::sql::drivers
