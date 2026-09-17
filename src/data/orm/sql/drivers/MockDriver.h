#pragma once

#include "data/orm/sql/Connection.h"
#include <string>
#include <vector>

namespace aegon::data::orm::sql::drivers {

class MockConnection : public Connection {
public:
    DatabaseDialect dialect_{DatabaseDialect::PostgreSQL};
    bool is_valid_{true};
    bool in_transaction_{false};
    int begin_count_{0};
    int commit_count_{0};
    int rollback_count_{0};

    std::vector<std::string> executed_sqls_;
    std::vector<std::vector<std::string>> executed_params_;
    std::vector<MockRowView> mock_results_;
    size_t next_execute_result_{1};

    explicit MockConnection(DatabaseDialect dialect = DatabaseDialect::PostgreSQL)
        : dialect_(dialect) {}

    core::Task<size_t> execute(std::string_view sql, const std::vector<std::string>& params) override {
        executed_sqls_.emplace_back(sql);
        executed_params_.push_back(params);
        co_return next_execute_result_;
    }

    core::Task<std::vector<MockRowView>> query(std::string_view sql, const std::vector<std::string>& params) override {
        executed_sqls_.emplace_back(sql);
        executed_params_.push_back(params);
        co_return mock_results_;
    }

    core::Task<void> begin_transaction() override {
        in_transaction_ = true;
        ++begin_count_;
        co_return;
    }

    core::Task<void> commit_transaction() override {
        in_transaction_ = false;
        ++commit_count_;
        co_return;
    }

    core::Task<void> rollback_transaction() override {
        in_transaction_ = false;
        ++rollback_count_;
        co_return;
    }

    [[nodiscard]] DatabaseDialect dialect() const noexcept override {
        return dialect_;
    }

    [[nodiscard]] bool is_valid() const noexcept override {
        return is_valid_;
    }
};

} // namespace aegon::data::orm::sql::drivers
