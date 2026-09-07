#pragma once

#include "Connection.h"
#include "Table.h"
#include "SelectBuilder.h"
#include "InsertBuilder.h"
#include "UpdateBuilder.h"
#include "DeleteBuilder.h"
#include "core/Task.h"
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

namespace aegon::data::orm::sql {

class Transaction {
    Connection& conn_;
    DatabaseDialect dialect_;
    bool committed_{false};
    bool rolled_back_{false};

public:
    explicit Transaction(Connection& conn)
        : conn_(conn), dialect_(conn.dialect()) {}

    [[nodiscard]] DatabaseDialect dialect() const noexcept { return dialect_; }
    [[nodiscard]] Connection& connection() noexcept { return conn_; }

    core::Task<void> begin() {
        co_await conn_.begin_transaction();
    }

    core::Task<void> commit() {
        if (!committed_ && !rolled_back_) {
            co_await conn_.commit_transaction();
            committed_ = true;
        }
    }

    core::Task<void> rollback() {
        if (!committed_ && !rolled_back_) {
            co_await conn_.rollback_transaction();
            rolled_back_ = true;
        }
    }

    [[nodiscard]] bool is_completed() const noexcept {
        return committed_ || rolled_back_;
    }

    // Entity Operations
    template <typename Entity>
    core::Task<void> insert(const Entity& entity) {
        auto query = insert_into<Entity>().values(entity).to_sql(dialect_);
        co_await conn_.execute(query.sql, query.params);
    }

    template <typename Entity>
    core::Task<size_t> update_entity(const Entity& entity) {
        auto schema = Entity::schema();
        const auto& pk_name = schema.primary_key_name();
        auto extracted = schema.extract_values(entity, false);

        std::string pk_val;
        for (const auto& [col, val] : extracted) {
            if (col == pk_name) {
                pk_val = val;
                break;
            }
        }

        auto builder = update<Entity>().set_entity(entity).where(pk_name, Op::Eq, pk_val);
        auto query = builder.to_sql(dialect_);
        co_return co_await conn_.execute(query.sql, query.params);
    }

    template <typename Entity, typename ID>
    core::Task<std::optional<Entity>> find_by_id(const ID& id) {
        auto schema = Entity::schema();
        auto builder = from<Entity>().where(schema.primary_key_name(), Op::Eq, id).limit(1);
        auto query = builder.to_sql(dialect_);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty()) {
            co_return std::nullopt;
        }
        co_return builder.map_row(rows[0]);
    }

    template <typename Entity, typename ID>
    core::Task<bool> delete_by_id(const ID& id) {
        auto schema = Entity::schema();
        auto query = delete_from<Entity>().where(schema.primary_key_name(), Op::Eq, id).to_sql(dialect_);
        size_t count = co_await conn_.execute(query.sql, query.params);
        co_return count > 0;
    }

    template <typename Entity>
    core::Task<std::vector<Entity>> fetch_all(const SelectBuilder<Entity>& builder) {
        auto query = builder.to_sql(dialect_);
        auto rows = co_await conn_.query(query.sql, query.params);
        co_return builder.map_rows(rows);
    }

    template <typename Entity>
    core::Task<std::optional<Entity>> fetch_one(const SelectBuilder<Entity>& builder) {
        auto query = builder.to_sql(dialect_);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty()) {
            co_return std::nullopt;
        }
        co_return builder.map_row(rows[0]);
    }

    core::Task<size_t> execute(const QueryResult& query) {
        co_return co_await conn_.execute(query.sql, query.params);
    }

    core::Task<size_t> execute(std::string_view sql, const std::vector<std::string>& params = {}) {
        co_return co_await conn_.execute(sql, params);
    }

    template <typename Entity>
    [[nodiscard]] SelectBuilder<Entity> from() const {
        return SelectBuilder<Entity>();
    }

    template <typename Entity>
    [[nodiscard]] UpdateBuilder<Entity> update() const {
        return UpdateBuilder<Entity>();
    }

    template <typename Entity>
    [[nodiscard]] DeleteBuilder<Entity> delete_from() const {
        return DeleteBuilder<Entity>();
    }
};

} // namespace aegon::data::orm::sql
