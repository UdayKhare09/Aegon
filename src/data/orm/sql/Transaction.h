#pragma once

#include "Connection.h"
#include "Table.h"
#include "SelectBuilder.h"
#include "InsertBuilder.h"
#include "UpdateBuilder.h"
#include "DeleteBuilder.h"
#include "core/Task.h"
#include "OptimisticLockException.h"
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
    core::Task<void> insert_all(std::span<Entity> entities) {
        for (const auto& entity : entities) {
            co_await insert(entity);
        }
    }

    template <typename Entity>
    core::Task<int64_t> insert_get_id(Entity& entity) {
        auto schema = Entity::schema();
        int64_t pid = co_await schema.insert_entity(entity, conn_);
        co_return pid;
    }

    template <typename Entity>
    core::Task<int64_t> insert_get_id(const Entity& entity) {
        auto query = insert_into<Entity>().values(entity).to_sql(dialect_);
        if (dialect_ == DatabaseDialect::PostgreSQL || dialect_ == DatabaseDialect::SQLite) {
            auto rows = co_await conn_.query(query.sql, query.params);
            if (!rows.empty()) {
                co_return rows[0].template get<int64_t>(0);
            }
            co_return 0;
        } else {
            co_await conn_.execute(query.sql, query.params);
            co_return 0;
        }
    }

    template <typename Entity>
    core::Task<void> insert_tree(Entity& entity) {
        auto schema = Entity::schema();
        int64_t pid = co_await insert_get_id(entity);
        std::string parent_pk = schema.get_primary_key(entity);
        if (parent_pk.empty() && pid > 0) {
            parent_pk = std::to_string(pid);
        }

        for (const auto& rel : schema.relations()) {
            if (rel.tree_inserter) {
                co_await rel.tree_inserter(entity, conn_, dialect_, parent_pk);
            }
        }
    }

    template <typename Entity>
    core::Task<size_t> update_entity(Entity& entity) {
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
        int64_t curr_ver = 0;
        int64_t next_ver = 0;
        if (schema.has_version()) {
            curr_ver = schema.get_version(entity);
            next_ver = curr_ver + 1;
            builder.set(schema.version_column(), next_ver);
            builder.where(schema.version_column(), Op::Eq, curr_ver);
        }

        auto query = builder.to_sql(dialect_);
        size_t n = co_await conn_.execute(query.sql, query.params);
        if (schema.has_version()) {
            if (n == 0) {
                throw OptimisticLockException("Optimistic lock failure: entity was modified concurrently");
            }
            schema.set_version(entity, next_ver);
        }
        co_return n;
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
        int64_t curr_ver = 0;
        int64_t next_ver = 0;
        if (schema.has_version()) {
            curr_ver = schema.get_version(entity);
            next_ver = curr_ver + 1;
            builder.set(schema.version_column(), next_ver);
            builder.where(schema.version_column(), Op::Eq, curr_ver);
        }

        auto query = builder.to_sql(dialect_);
        size_t n = co_await conn_.execute(query.sql, query.params);
        if (schema.has_version()) {
            if (n == 0) {
                throw OptimisticLockException("Optimistic lock failure: entity was modified concurrently");
            }
            schema.set_version(const_cast<Entity&>(entity), next_ver);
        }
        co_return n;
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
        auto results = builder.map_rows(rows);
        if (builder.has_includes() && !results.empty()) {
            co_await builder.eager_load_includes(results, conn_, dialect_);
        }
        co_return results;
    }

    template <typename Entity>
    core::Task<std::optional<Entity>> fetch_one(const SelectBuilder<Entity>& builder) {
        auto query = builder.to_sql(dialect_);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty()) {
            co_return std::nullopt;
        }
        Entity item = builder.map_row(rows[0]);
        if (builder.has_includes()) {
            std::vector<Entity> vec;
            vec.push_back(std::move(item));
            co_await builder.eager_load_includes(vec, conn_, dialect_);
            co_return std::move(vec[0]);
        }
        co_return item;
    }

    template <typename Entity>
    core::Task<uint64_t> count(const SelectBuilder<Entity>& builder) {
        auto query = builder.to_count_sql(dialect_);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty() || rows[0].is_null(0)) {
            co_return 0;
        }
        co_return rows[0].template get<uint64_t>(0);
    }

    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> sum(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        using Target = unwrapped_type_t<FieldType>;
        auto query = builder.to_aggregate_sql(dialect_, "SUM", field);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty() || rows[0].is_null(0)) {
            co_return std::nullopt;
        }
        co_return rows[0].template get<Target>(0);
    }

    template <typename ResultType, typename Entity, typename FieldType>
    core::Task<std::optional<ResultType>> sum(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        auto query = builder.to_aggregate_sql(dialect_, "SUM", field);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty() || rows[0].is_null(0)) {
            co_return std::nullopt;
        }
        co_return rows[0].template get<ResultType>(0);
    }

    template <typename Entity, typename FieldType>
    core::Task<std::optional<double>> avg(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        auto query = builder.to_aggregate_sql(dialect_, "AVG", field);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty() || rows[0].is_null(0)) {
            co_return std::nullopt;
        }
        co_return rows[0].template get<double>(0);
    }

    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> min(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        using Target = unwrapped_type_t<FieldType>;
        auto query = builder.to_aggregate_sql(dialect_, "MIN", field);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty() || rows[0].is_null(0)) {
            co_return std::nullopt;
        }
        co_return rows[0].template get<Target>(0);
    }

    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> max(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        using Target = unwrapped_type_t<FieldType>;
        auto query = builder.to_aggregate_sql(dialect_, "MAX", field);
        auto rows = co_await conn_.query(query.sql, query.params);
        if (rows.empty() || rows[0].is_null(0)) {
            co_return std::nullopt;
        }
        co_return rows[0].template get<Target>(0);
    }

    template <typename Entity>
    core::Task<Page<Entity>> paginate(SelectBuilder<Entity> builder, size_t page = 1, size_t per_page = 20) {
        if (per_page == 0) per_page = 20;
        if (page == 0) page = 1;

        uint64_t total = co_await count(builder);

        builder.limit(per_page).offset((page - 1) * per_page);
        auto items = co_await fetch_all(builder);

        Page<Entity> result;
        result.items = std::move(items);
        result.total_items = total;
        result.current_page = page;
        result.per_page = per_page;
        result.total_pages = total == 0 ? 0 : (total + per_page - 1) / per_page;
        result.has_next = page < result.total_pages;
        result.has_prev = page > 1 && result.total_pages > 0;

        co_return result;
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
