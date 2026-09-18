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
#include <chrono>

namespace aegon::data::orm::sql {

enum class DeferredCacheOpType {
    Del,
    Incr,
    Set
};

struct DeferredCacheOp {
    DeferredCacheOpType type;
    std::string key;
    std::string value{};
    std::chrono::seconds ttl{300};
};

class Transaction {
    Connection& conn_;
    DatabaseDialect dialect_;
    bool committed_{false};
    bool rolled_back_{false};
    std::vector<DeferredCacheOp>* deferred_cache_ops_{nullptr};

public:
    explicit Transaction(Connection& conn)
        : conn_(conn), dialect_(conn.dialect()) {}

    void set_deferred_cache_ops(std::vector<DeferredCacheOp>* ops) noexcept {
        deferred_cache_ops_ = ops;
    }

    [[nodiscard]] std::vector<DeferredCacheOp>* deferred_cache_ops() const noexcept {
        return deferred_cache_ops_;
    }

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

    template <typename Entity>
    void record_insert_cache(const Entity& entity, int64_t new_id = 0) {
        if (!deferred_cache_ops_) return;
        auto schema = Entity::schema();
        if (!schema.is_cached()) return;

        if (schema.cache_config().invalidation == InvalidationMode::Partitioned && schema.cache_config().partition_extractor) {
            std::string part_key = schema.table_name() + ":part:" + schema.cache_config().partition_extractor(entity) + ":epoch";
            deferred_cache_ops_->push_back({DeferredCacheOpType::Incr, std::move(part_key), {}, {}});
        } else if (schema.cache_config().invalidation == InvalidationMode::StrictEpoch) {
            deferred_cache_ops_->push_back({DeferredCacheOpType::Incr, schema.table_name() + ":epoch", {}, {}});
        } else if (schema.cache_config().invalidation == InvalidationMode::PredicateAware) {
            auto vals = schema.extract_values(entity, false);
            for (const auto& [col, val] : vals) {
                if (!val.empty()) {
                    deferred_cache_ops_->push_back({DeferredCacheOpType::Incr, schema.table_name() + ":pred:" + col + ":" + val + ":epoch", {}, {}});
                }
            }
        }

        if (schema.cache_config().by_id) {
            std::string pk_val;
            if (new_id > 0) {
                pk_val = std::to_string(new_id);
            } else {
                auto vals = schema.extract_values(entity, false);
                for (const auto& [col, val] : vals) {
                    if (col == schema.primary_key_name() && !val.empty()) {
                        pk_val = val;
                        break;
                    }
                }
            }
            if (!pk_val.empty()) {
                std::string id_key = schema.table_name() + ":id:" + pk_val;
                deferred_cache_ops_->push_back({DeferredCacheOpType::Set, std::move(id_key), schema.serialize_entity_json(entity), schema.cache_config().ttl});
            }
        }
    }

    template <typename Entity>
    void record_update_cache(const Entity& entity) {
        if (!deferred_cache_ops_) return;
        auto schema = Entity::schema();
        if (!schema.is_cached()) return;

        std::string pk_str;
        auto vals = schema.extract_values(entity, false);
        for (const auto& [col, val] : vals) {
            if (col == schema.primary_key_name()) {
                pk_str = val;
                break;
            }
        }

        if (!pk_str.empty()) {
            std::string id_key = schema.table_name() + ":id:" + pk_str;
            if (schema.cache_config().mutation_sync == MutationSync::UpdateOnWrite) {
                deferred_cache_ops_->push_back({DeferredCacheOpType::Set, std::move(id_key), schema.serialize_entity_json(entity), schema.cache_config().ttl});
            } else {
                deferred_cache_ops_->push_back({DeferredCacheOpType::Del, std::move(id_key), {}, {}});
            }
        }

        if (schema.cache_config().unique_column && schema.cache_config().unique_extractor) {
            std::string u_key = schema.table_name() + ":" + *schema.cache_config().unique_column + ":" + schema.cache_config().unique_extractor(entity);
            deferred_cache_ops_->push_back({DeferredCacheOpType::Del, std::move(u_key), {}, {}});
        }

        if (schema.cache_config().invalidation == InvalidationMode::Partitioned && schema.cache_config().partition_extractor) {
            std::string part_key = schema.table_name() + ":part:" + schema.cache_config().partition_extractor(entity) + ":epoch";
            deferred_cache_ops_->push_back({DeferredCacheOpType::Incr, std::move(part_key), {}, {}});
        } else if (schema.cache_config().invalidation == InvalidationMode::StrictEpoch) {
            deferred_cache_ops_->push_back({DeferredCacheOpType::Incr, schema.table_name() + ":epoch", {}, {}});
        } else if (schema.cache_config().invalidation == InvalidationMode::PredicateAware) {
            for (const auto& [col, val] : vals) {
                if (!val.empty()) {
                    deferred_cache_ops_->push_back({DeferredCacheOpType::Incr, schema.table_name() + ":pred:" + col + ":" + val + ":epoch", {}, {}});
                }
            }
        }
    }

    template <typename Entity, typename ID>
    void record_delete_cache(const ID& id) {
        if (!deferred_cache_ops_) return;
        auto schema = Entity::schema();
        if (!schema.is_cached()) return;

        std::string id_str = format_param_value(id);
        std::string id_key = schema.table_name() + ":id:" + id_str;
        deferred_cache_ops_->push_back({DeferredCacheOpType::Del, std::move(id_key), {}, {}});

        if (schema.cache_config().invalidation == InvalidationMode::StrictEpoch) {
            deferred_cache_ops_->push_back({DeferredCacheOpType::Incr, schema.table_name() + ":epoch", {}, {}});
        } else if (schema.cache_config().invalidation == InvalidationMode::PredicateAware) {
            deferred_cache_ops_->push_back({DeferredCacheOpType::Incr, schema.table_name() + ":pred:" + schema.primary_key_name() + ":" + id_str + ":epoch", {}, {}});
        }
    }

    // Entity Operations
    template <typename Entity>
    core::Task<void> insert(Entity& entity) {
        auto schema = Entity::schema();
        if (schema.has_auto_increment_pk()) {
            co_await insert_get_id(entity);
            co_return;
        }
        auto query = insert_into<Entity>().values(entity).to_sql(dialect_);
        co_await conn_.execute(query.sql, query.params);
        record_insert_cache(entity);
    }

    template <typename Entity>
    core::Task<void> insert(const Entity& entity) {
        auto query = insert_into<Entity>().values(entity).to_sql(dialect_);
        co_await conn_.execute(query.sql, query.params);
        record_insert_cache(entity);
    }

    template <typename Entity>
    core::Task<void> insert_all(std::span<Entity> entities) {
        for (auto& entity : entities) {
            co_await insert(entity);
        }
    }

    template <typename Entity>
    core::Task<int64_t> insert_get_id(Entity& entity) {
        auto schema = Entity::schema();
        int64_t pid = co_await schema.insert_entity(entity, conn_);
        record_insert_cache(entity, pid);
        co_return pid;
    }

    template <typename Entity>
    core::Task<int64_t> insert_get_id(const Entity& entity) {
        auto query = insert_into<Entity>().values(entity).to_sql(dialect_);
        int64_t pid = 0;
        if (dialect_ == DatabaseDialect::PostgreSQL || dialect_ == DatabaseDialect::SQLite) {
            auto rows = co_await conn_.query(query.sql, query.params);
            if (!rows.empty()) {
                pid = rows[0].template get<int64_t>(0);
            }
        } else {
            co_await conn_.execute(query.sql, query.params);
        }
        record_insert_cache(entity, pid);
        co_return pid;
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
        if (n > 0) {
            record_update_cache(entity);
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
        if (n > 0) {
            record_update_cache(entity);
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

    template <typename Entity, typename FieldType, typename ValueType>
    core::Task<std::optional<Entity>> find_by_unique(FieldType Entity::* field, const ValueType& val) {
        auto q = from<Entity>().where(field, Op::Eq, val);
        co_return co_await fetch_one(q);
    }

    template <typename Entity, typename ID>
    core::Task<bool> delete_by_id(const ID& id) {
        auto schema = Entity::schema();
        auto query = delete_from<Entity>().where(schema.primary_key_name(), Op::Eq, id).to_sql(dialect_);
        size_t count = co_await conn_.execute(query.sql, query.params);
        if (count > 0) {
            record_delete_cache<Entity>(id);
        }
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

    template <typename JunctionEntity, typename ParentID, typename ChildID>
    core::Task<void> link(const ParentID& parent_id, const ChildID& child_id) {
        auto schema = JunctionEntity::schema();
        std::string sql = "INSERT INTO ";
        sql.append(DialectTraits::quote_identifier(dialect_, schema.table_name()));
        sql.append(" (");
        sql.append(DialectTraits::quote_identifier(dialect_, schema.columns()[0].column_name));
        sql.append(", ");
        sql.append(DialectTraits::quote_identifier(dialect_, schema.columns()[1].column_name));
        sql.append(") VALUES (");
        std::string p1, p2;
        DialectTraits::format_placeholder(dialect_, 1, p1);
        DialectTraits::format_placeholder(dialect_, 2, p2);
        sql.append(p1);
        sql.append(", ");
        sql.append(p2);
        sql.append(");");

        co_await conn_.execute(sql, {format_param_value(parent_id), format_param_value(child_id)});
    }

    template <typename JunctionEntity, typename ParentID, typename ChildID>
    core::Task<bool> unlink(const ParentID& parent_id, const ChildID& child_id) {
        auto schema = JunctionEntity::schema();
        std::string sql = "DELETE FROM ";
        sql.append(DialectTraits::quote_identifier(dialect_, schema.table_name()));
        sql.append(" WHERE ");
        sql.append(DialectTraits::quote_identifier(dialect_, schema.columns()[0].column_name));
        sql.append(" = ");
        std::string p1;
        DialectTraits::format_placeholder(dialect_, 1, p1);
        sql.append(p1);
        sql.append(" AND ");
        sql.append(DialectTraits::quote_identifier(dialect_, schema.columns()[1].column_name));
        sql.append(" = ");
        std::string p2;
        DialectTraits::format_placeholder(dialect_, 2, p2);
        sql.append(p2);
        sql.append(";");

        size_t n = co_await conn_.execute(sql, {format_param_value(parent_id), format_param_value(child_id)});
        co_return n > 0;
    }

    template <typename Relation>
    core::Task<void> load(Relation& rel) {
        co_await rel.load(conn_);
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
