#pragma once

#include "PerCoreConnectionPool.h"
#include "Transaction.h"
#include "SelectBuilder.h"
#include "InsertBuilder.h"
#include "UpdateBuilder.h"
#include "DeleteBuilder.h"
#include "OptimisticLockException.h"
#include "SchemaGenerator.h"
#include "core/Task.h"
#include <concepts>
#include <functional>
#include <optional>
#include <vector>
#include <exception>
#include <atomic>
#include <algorithm>

#include "OrmCache.h"
#include "data/cache/CacheBackend.h"

namespace aegon::data::orm::sql {

namespace detail {
template <typename T>
struct task_value {
    using type = T;
};

template <typename T>
struct task_value<core::Task<T>> {
    using type = T;
};

template <typename T>
using task_value_t = typename task_value<T>::type;
} // namespace detail

class SqlDatabaseClient {
    PerCoreConnectionPool& primary_pool_;
    std::vector<std::reference_wrapper<PerCoreConnectionPool>> replica_pools_;
    mutable std::atomic<size_t> rr_counter_{0};
    std::shared_ptr<cache::CacheBackend> cache_{nullptr};

public:
    explicit SqlDatabaseClient(PerCoreConnectionPool& pool)
        : primary_pool_(pool) {}

    SqlDatabaseClient(PerCoreConnectionPool& primary, std::vector<std::reference_wrapper<PerCoreConnectionPool>> replicas)
        : primary_pool_(primary), replica_pools_(std::move(replicas)) {}

    [[nodiscard]] PerCoreConnectionPool& pool() noexcept { return primary_pool_; }
    [[nodiscard]] PerCoreConnectionPool& primary_pool() noexcept { return primary_pool_; }

    [[nodiscard]] PerCoreConnectionPool& read_pool() const noexcept {
        if (replica_pools_.empty()) {
            return primary_pool_;
        }
        size_t idx = rr_counter_.fetch_add(1, std::memory_order_relaxed) % replica_pools_.size();
        return replica_pools_[idx].get();
    }

    [[nodiscard]] const std::vector<std::reference_wrapper<PerCoreConnectionPool>>& replica_pools() const noexcept {
        return replica_pools_;
    }

    void set_cache(std::shared_ptr<cache::CacheBackend> cache) noexcept {
        cache_ = std::move(cache);
    }

    [[nodiscard]] std::shared_ptr<cache::CacheBackend> cache() const noexcept {
        return cache_;
    }

    // Helper to flush deferred cache operations
    core::Task<void> flush_deferred_cache(const std::vector<DeferredCacheOp>& ops) {
        if (!cache_ || ops.empty()) co_return;
        co_await aegon::data::orm::sql::flush_deferred_cache(*cache_, ops);
    }

    // Generalized execution wrappers for primary pool (with deferred cache) and read pool
    /**
     * @brief Await action(tx) on a primary connection, then flush recorded cache operations.
     * Return the action's result, if any. This does not begin, commit, or roll back a
     * database transaction. Keep action alive until the task completes. Pool and
     * action errors propagate and prevent the flush; cache errors propagate after
     * the action has completed and may leave some cache operations applied.
     */
    template <typename Action>
    auto execute_with_tx(Action&& action) -> core::Task<detail::task_value_t<std::invoke_result_t<Action, Transaction&>>> {
        using R = detail::task_value_t<std::invoke_result_t<Action, Transaction&>>;
        auto guard = primary_pool_.acquire();
        Transaction tx(*guard);
        std::vector<DeferredCacheOp> deferred;
        if (cache_) tx.set_deferred_cache_ops(&deferred);
        if constexpr (std::is_void_v<R>) {
            co_await action(tx);
            co_await flush_deferred_cache(deferred);
        } else {
            R res = co_await action(tx);
            co_await flush_deferred_cache(deferred);
            co_return res;
        }
    }

    /**
     * @brief Await action(tx) on a read connection and return its result, if any.
     * Use replicas in round-robin order, or the primary when none are configured.
     * No database transaction is begun and no deferred cache buffer is installed.
     * Keep action alive until completion; pool and action errors propagate.
     */
    template <typename Action>
    auto execute_with_read_tx(Action&& action) -> core::Task<detail::task_value_t<std::invoke_result_t<Action, Transaction&>>> {
        using R = detail::task_value_t<std::invoke_result_t<Action, Transaction&>>;
        auto guard = read_pool().acquire();
        Transaction tx(*guard);
        if constexpr (std::is_void_v<R>) {
            co_await action(tx);
        } else {
            co_return co_await action(tx);
        }
    }

    // Transaction & Unit of Work Lifecycle (always on primary pool)
    template <typename Func>
    core::Task<void> transaction(Func&& block) {
        auto guard = primary_pool_.acquire();
        Transaction tx(*guard);
        std::vector<DeferredCacheOp> deferred_cache;
        if (cache_) {
            tx.set_deferred_cache_ops(&deferred_cache);
        }
        co_await tx.begin();

        std::exception_ptr ex{nullptr};
        try {
            co_await block(tx);
        } catch (...) {
            ex = std::current_exception();
        }

        if (ex) {
            if (!tx.is_completed()) {
                co_await tx.rollback();
            }
            std::rethrow_exception(ex);
        } else if (!tx.is_completed()) {
            co_await tx.commit();
            co_await flush_deferred_cache(deferred_cache);
        }
    }

    // Direct non-transactional single-operation conveniences
    /**
     * @brief Insert on the primary, assigning a positive generated auto-increment key to
     * entity.
     * Initialize its relation loaders when using the generated-key path and flush
     * recorded cache operations after success. Pool, database, and cache errors propagate; a
     * cache error can follow a successful write.
     */
    template <typename Entity>
    core::Task<void> insert(Entity& entity) {
        auto schema = Entity::schema();
        if (schema.has_auto_increment_pk()) {
            co_await insert_get_id(entity);
            co_return;
        }
        co_await execute_with_tx([&](Transaction& tx) { return tx.insert(entity); });
    }

    /**
     * @brief Insert on the primary without assigning generated keys to entity.
     * Flush recorded cache operations after success. Pool, database, and cache errors
     * propagate; a cache error can follow a successful write.
     */
    template <typename Entity>
    core::Task<void> insert(const Entity& entity) {
        co_await execute_with_tx([&](Transaction& tx) { return tx.insert(entity); });
    }

    /**
     * @brief Insert on the primary and return the generated key, or zero if none is obtained.
     * Assign a positive generated key to entity and initialize its relation loaders.
     * Flush recorded cache operations after success. Pool, database, and cache errors
     * propagate; a cache error can follow a successful write.
     */
    template <typename Entity>
    core::Task<int64_t> insert_get_id(Entity& entity) {
        co_return co_await execute_with_tx([&](Transaction& tx) { return tx.insert_get_id(entity); });
    }

    /**
     * @brief Insert on the primary and return the first returned key, or zero for no row.
     * The entity is unchanged. Flush recorded cache operations after success.
     * Pool, database, and cache errors propagate; a cache error can follow a successful write.
     */
    template <typename Entity>
    core::Task<int64_t> insert_get_id(const Entity& entity) {
        co_return co_await execute_with_tx([&](Transaction& tx) { return tx.insert_get_id(entity); });
    }

    /**
     * @brief Insert entities sequentially on one primary connection, assigning generated keys.
     * Empty input performs no inserts. No transaction is begun: earlier inserts can
     * remain if a later insert fails. Flush recorded cache operations only after all
     * inserts succeed. Pool, database, and cache errors propagate; a cache error can follow a
     * successful write.
     */
    template <typename Entity>
    core::Task<void> insert_all(std::span<Entity> entities) {
        co_await execute_with_tx([&](Transaction& tx) { return tx.insert_all(entities); });
    }

    template <typename Entity>
    core::Task<void> insert_tree(Entity& entity) {
        co_await transaction([&](Transaction& tx) -> core::Task<void> {
            co_await tx.insert_tree(entity);
        });
    }

    /**
     * @brief Update by primary key on the primary and return the affected row count.
     * For a versioned schema, also match the current version and write the next
     * version back to entity after success, including through the const overload.
     * A versioned entity must therefore be modifiable. Flush recorded cache operations.
     * @throws OptimisticLockException If a versioned update affects no rows.
     * Pool, database, and cache errors propagate; a cache error can follow a successful write.
     */
    template <typename Entity>
    core::Task<size_t> update_entity(Entity& entity) {
        co_return co_await execute_with_tx([&](Transaction& tx) { return tx.update_entity(entity); });
    }

    /**
     * @brief Update by primary key on the primary and return the affected row count.
     * For a versioned schema, also match the current version and write the next
     * version back to entity after success, including through the const overload.
     * A versioned entity must therefore be modifiable. Flush recorded cache operations.
     * @throws OptimisticLockException If a versioned update affects no rows.
     * Pool, database, and cache errors propagate; a cache error can follow a successful write.
     */
    template <typename Entity>
    core::Task<size_t> update_entity(const Entity& entity) {
        co_return co_await execute_with_tx([&](Transaction& tx) { return tx.update_entity(entity); });
    }

    template <typename Entity, typename ID>
    core::Task<std::optional<Entity>> find_by_id(const ID& id) {
        auto schema = Entity::schema();
        std::string id_str = make_sql_param(id).to_debug_string();
        std::string id_key = format_cache_id_key(schema.table_name(), id_str);

        if (cache_ && schema.is_cached() && schema.cache_config().by_id) {
            auto cached_json = co_await cache_->get(id_key);
            if (cached_json.has_value()) {
                Entity e{};
                if (schema.deserialize_entity_json(*cached_json, e)) {
                    co_return e;
                }
            }
        }

        auto guard = read_pool().acquire();
        Transaction tx(*guard);
        auto opt_entity = co_await tx.find_by_id<Entity>(id);

        if (opt_entity && cache_ && schema.is_cached() && schema.cache_config().by_id) {
            std::string json = schema.serialize_entity_json(*opt_entity);
            co_await cache_->set(id_key, json, schema.cache_config().ttl);
            if (schema.cache_config().unique_column && schema.cache_config().unique_extractor) {
                std::string u_key = format_cache_unique_key(schema.table_name(), *schema.cache_config().unique_column, schema.cache_config().unique_extractor(*opt_entity));
                co_await cache_->set(u_key, id_str, schema.cache_config().ttl);
            }
        }

        co_return opt_entity;
    }

    /**
     * @brief Return the first entity matching field, or nullopt if no row matches.
     * Try a configured unique-value cache mapping first, otherwise query a read
     * connection and populate entity and unique-value cache entries when enabled.
     * Pool, database, and cache errors propagate rather than becoming cache misses.
     */
    template <typename Entity, typename FieldType, typename ValueType>
    core::Task<std::optional<Entity>> find_by_unique(FieldType Entity::* field, const ValueType& val) {
        auto schema = Entity::schema();
        std::string col_name = schema.resolve_column_name(field);
        std::string val_str = make_sql_param(val).to_debug_string();

        if (cache_ && schema.is_cached() && schema.cache_config().unique_column && *schema.cache_config().unique_column == col_name) {
            std::string u_key = format_cache_unique_key(schema.table_name(), col_name, val_str);
            auto opt_pk = co_await cache_->get(u_key);
            if (opt_pk) {
                auto by_id_res = co_await find_by_id<Entity>(*opt_pk);
                if (by_id_res) co_return by_id_res;
            }
        }

        auto guard = read_pool().acquire();
        Transaction tx(*guard);
        auto opt_entity = co_await tx.find_by_unique(field, val);
        if (opt_entity && cache_ && schema.is_cached()) {
            std::string pk = schema.get_primary_key(*opt_entity);
            if (!pk.empty()) {
                std::string id_key = format_cache_id_key(schema.table_name(), pk);
                co_await cache_->set(id_key, schema.serialize_entity_json(*opt_entity), schema.cache_config().ttl);
                std::string u_key = format_cache_unique_key(schema.table_name(), col_name, val_str);
                co_await cache_->set(u_key, pk, schema.cache_config().ttl);
            }
        }
        co_return opt_entity;
    }

    /**
     * @brief Delete matching primary-key rows on the primary; return whether any were deleted.
     * Flush recorded cache operations after success. Pool, database, and cache errors
     * propagate; a cache error can follow a successful write.
     */
    template <typename Entity, typename ID>
    core::Task<bool> delete_by_id(const ID& id) {
        co_return co_await execute_with_tx([&](Transaction& tx) { return tx.template delete_by_id<Entity>(id); });
    }

    /**
     * @brief Fetch mapped entities, using the configured query cache when enabled.
     * A cache miss or failed entity deserialization triggers a read-pool query;
     * cache entries are populated with the resolved query TTL in seconds. Includes
     * are eagerly loaded for nonempty results, including cache hits. Pool, database,
     * and cache errors propagate; cache exceptions do not trigger a fallback query.
     */
    template <typename Entity>
    core::Task<std::vector<Entity>> fetch_all(const SelectBuilder<Entity>& builder) {
        auto schema = builder.schema();
        bool use_cache = (cache_ != nullptr) && builder.is_cached();

        std::string query_key;
        if (use_cache) {
            auto guard = read_pool().acquire();
            auto dialect = guard->dialect();
            auto q = builder.to_sql(dialect);
            std::string fp = builder.compute_query_fingerprint(q);

            std::string epoch = "0";
            auto mode = builder.invalidation_mode();
            if (mode == InvalidationMode::TtlOnly) {
                query_key = schema.table_name() + ":q:ttl:" + fp;
            } else if (mode == InvalidationMode::Partitioned) {
                auto part_val = builder.resolve_partition_value();
                if (part_val) {
                    std::string part_epoch_key = schema.table_name() + ":part:" + *part_val + ":epoch";
                    auto epoch_opt = co_await cache_->get(part_epoch_key);
                    if (epoch_opt) epoch = *epoch_opt;
                    query_key = schema.table_name() + ":q:part:" + *part_val + ":v" + epoch + ":" + fp;
                } else {
                    std::string epoch_key = schema.table_name() + ":epoch";
                    auto epoch_opt = co_await cache_->get(epoch_key);
                    if (epoch_opt) epoch = *epoch_opt;
                    query_key = schema.table_name() + ":q:v" + epoch + ":" + fp;
                }
            } else if (mode == InvalidationMode::PredicateAware) {
                auto preds = builder.extract_equality_predicates();
                if (!preds.empty()) {
                    std::sort(preds.begin(), preds.end());
                    std::vector<std::string> pred_epoch_keys;
                    pred_epoch_keys.reserve(preds.size());
                    for (const auto& [col, val] : preds) {
                        pred_epoch_keys.push_back(schema.table_name() + ":pred:" + col + ":" + val + ":epoch");
                    }
                    auto epoch_opts = co_await cache_->mget(pred_epoch_keys);
                    std::string pred_part;
                    for (size_t i = 0; i < preds.size(); ++i) {
                        std::string p_epoch = (i < epoch_opts.size() && epoch_opts[i].has_value()) ? *epoch_opts[i] : "0";
                        pred_part += ":" + preds[i].first + "=" + preds[i].second + ":v" + p_epoch;
                    }
                    query_key = schema.table_name() + ":q:pred" + pred_part + ":" + fp;
                } else {
                    std::string epoch_key = schema.table_name() + ":epoch";
                    auto epoch_opt = co_await cache_->get(epoch_key);
                    if (epoch_opt) epoch = *epoch_opt;
                    query_key = schema.table_name() + ":q:v" + epoch + ":" + fp;
                }
            } else {
                std::string epoch_key = schema.table_name() + ":epoch";
                auto epoch_opt = co_await cache_->get(epoch_key);
                if (epoch_opt) epoch = *epoch_opt;
                query_key = schema.table_name() + ":q:v" + epoch + ":" + fp;
            }

            auto cached_ids = co_await cache_->get(query_key);
            if (cached_ids.has_value()) {
                std::vector<std::string> ids;
                std::string_view sv = *cached_ids;
                while (!sv.empty()) {
                    size_t comma = sv.find(',');
                    if (comma == std::string_view::npos) {
                        ids.emplace_back(sv);
                        break;
                    }
                    ids.emplace_back(sv.substr(0, comma));
                    sv.remove_prefix(comma + 1);
                }

                if (ids.empty()) co_return std::vector<Entity>{};

                std::vector<std::string> id_keys;
                id_keys.reserve(ids.size());
                for (const auto& i : ids) {
                    id_keys.push_back(schema.table_name() + ":id:" + i);
                }

                auto mget_res = co_await cache_->mget(id_keys);
                bool all_hit = true;
                std::vector<Entity> entities;
                entities.reserve(ids.size());
                for (const auto& json_opt : mget_res) {
                    if (!json_opt.has_value()) {
                        all_hit = false;
                        break;
                    }
                    Entity e{};
                    if (!schema.deserialize_entity_json(*json_opt, e)) {
                        all_hit = false;
                        break;
                    }
                    entities.push_back(std::move(e));
                }

                if (all_hit) {
                    if (builder.has_includes() && !entities.empty()) {
                        auto guard = read_pool().acquire();
                        auto dialect = guard->dialect();
                        co_await builder.eager_load_includes(entities, *guard, dialect);
                    }
                    co_return entities;
                }
            }
        }

        auto guard = read_pool().acquire();
        Transaction tx(*guard);
        auto entities = co_await tx.fetch_all(builder);

        if (use_cache && !query_key.empty()) {
            std::string ids_str;
            auto ttl = builder.cache_ttl().value_or(std::chrono::seconds(300));
            for (size_t i = 0; i < entities.size(); ++i) {
                std::string pk = schema.get_primary_key(entities[i]);
                if (!pk.empty()) {
                    if (i > 0) ids_str.push_back(',');
                    ids_str.append(pk);
                    std::string id_key = schema.table_name() + ":id:" + pk;
                    co_await cache_->set(id_key, schema.serialize_entity_json(entities[i]), ttl);
                }
            }
            co_await cache_->set(query_key, ids_str, ttl);
        }

        co_return entities;
    }

    /**
     * @brief Query a read connection and replace out with mapped entities, reusing its
     * capacity.
     * Eagerly load configured includes. This path bypasses query caching.
     * Pool and database errors propagate.
     */
    template <typename Entity>
    core::Task<void> fetch_into(const SelectBuilder<Entity>& builder, std::vector<Entity>& out) {
        co_await execute_with_read_tx([&](Transaction& tx) { return tx.fetch_into(builder, out); });
    }

    template <typename Entity>
    core::Task<std::optional<Entity>> fetch_one(const SelectBuilder<Entity>& builder) {
        SelectBuilder<Entity> copy = builder;
        copy.limit(1);
        auto all = co_await fetch_all(copy);
        if (!all.empty()) {
            co_return all.front();
        }
        co_return std::nullopt;
    }

    /**
     * @brief Return the first COUNT(*) result from a read connection, or zero for no row or
     * NULL.
     * Keep WHERE, GROUP BY, and HAVING; ignore projections, ordering, limit, and offset.
     * With grouping, return only the first group's count. Pool and database errors propagate.
     */
    template <typename Entity>
    core::Task<uint64_t> count(const SelectBuilder<Entity>& builder) {
        co_return co_await execute_with_read_tx([&](Transaction& tx) { return tx.count(builder); });
    }

    /**
     * @brief Return the first SUM result from a read connection as the field's unwrapped type.
     * Return nullopt for no row or NULL. Keep WHERE, GROUP BY, and HAVING; ignore
     * ordering, limit, and offset. With grouping, use the first group. Pool and database errors
     * propagate.
     */
    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> sum(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        co_return co_await execute_with_read_tx([&](Transaction& tx) { return tx.sum(builder, field); });
    }

    /**
     * @brief Return the first SUM result from a read connection converted to ResultType.
     * Return nullopt for no row or NULL. Keep WHERE, GROUP BY, and HAVING; ignore
     * ordering, limit, and offset. With grouping, use the first group. Pool and database errors
     * propagate.
     */
    template <typename ResultType, typename Entity, typename FieldType>
    core::Task<std::optional<ResultType>> sum(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        co_return co_await execute_with_read_tx([&](Transaction& tx) { return tx.template sum<ResultType>(builder, field); });
    }

    /**
     * @brief Return the first AVG result from a read connection as double.
     * Return nullopt for no row or NULL. Keep WHERE, GROUP BY, and HAVING; ignore
     * ordering, limit, and offset. With grouping, use the first group. Pool and database errors
     * propagate.
     */
    template <typename Entity, typename FieldType>
    core::Task<std::optional<double>> avg(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        co_return co_await execute_with_read_tx([&](Transaction& tx) { return tx.avg(builder, field); });
    }

    /**
     * @brief Return the first MIN result from a read connection as the field's unwrapped type.
     * Return nullopt for no row or NULL. Keep WHERE, GROUP BY, and HAVING; ignore
     * ordering, limit, and offset. With grouping, use the first group. Pool and database errors
     * propagate.
     */
    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> min(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        co_return co_await execute_with_read_tx([&](Transaction& tx) { return tx.min(builder, field); });
    }

    /**
     * @brief Return the first MAX result from a read connection as the field's unwrapped type.
     * Return nullopt for no row or NULL. Keep WHERE, GROUP BY, and HAVING; ignore
     * ordering, limit, and offset. With grouping, use the first group. Pool and database errors
     * propagate.
     */
    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> max(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        co_return co_await execute_with_read_tx([&](Transaction& tx) { return tx.max(builder, field); });
    }

    /**
     * @brief Count then fetch a page on one read connection, including pagination metadata.
     * page is 1-based; zero becomes 1. per_page counts rows; zero becomes 20.
     * The copied builder's limit and offset are replaced. No transaction is begun
     * between the count and fetch, and query caching is bypassed. Pool and database errors
     * propagate.
     */
    template <typename Entity>
    core::Task<Page<Entity>> paginate(SelectBuilder<Entity> builder, size_t page = 1, size_t per_page = 20) {
        co_return co_await execute_with_read_tx([&](Transaction& tx) { return tx.paginate(std::move(builder), page, per_page); });
    }

    /**
     * @brief Execute parameterized SQL on the primary and return the driver's affected row
     * count.
     * No entity cache operations are inferred from raw SQL. Pool and database errors propagate.
     */
    core::Task<size_t> execute(const QueryResult& query) {
        co_return co_await execute_with_tx([&](Transaction& tx) { return tx.execute(query); });
    }

    /**
     * @brief Execute parameterized SQL on the primary and return the driver's affected row
     * count.
     * No entity cache operations are inferred from raw SQL. Pool and database errors propagate.
     */
    core::Task<size_t> execute(std::string_view sql, std::span<const SqlParam> params = {}) {
        co_return co_await execute_with_tx([&](Transaction& tx) { return tx.execute(sql, params); });
    }

    /**
     * @brief Automatically generates and executes DDL migrations for one or more entity schemas.
     */
    template <typename... Entities>
    core::Task<void> sync_schema() {
        DatabaseDialect d = DatabaseDialect::SQLite;
        {
            auto guard = primary_pool_.acquire();
            d = guard->dialect();
        }
        (co_await execute(generate_ddl<Entities>(d)), ...);
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

    template <typename Relation>
    core::Task<void> load(Relation& rel) {
        co_await rel.load(*this);
    }

    /**
     * @brief Insert a junction row on the primary using the first two non-auto-increment
     * columns.
     * Schema order maps parent_id to the first column and child_id to the second.
     * @throws std::runtime_error If fewer than two eligible columns exist.
     * Pool and database errors propagate.
     */
    template <typename JunctionEntity, typename ParentID, typename ChildID>
    core::Task<void> link(const ParentID& parent_id, const ChildID& child_id) {
        co_await execute_with_tx([&](Transaction& tx) { return tx.template link<JunctionEntity>(parent_id, child_id); });
    }

    /**
     * @brief Delete a junction row on the primary using the first two non-auto-increment
     * columns.
     * Schema order maps parent_id to the first column and child_id to the second.
     * Return whether any rows were deleted.
     * @throws std::runtime_error If fewer than two eligible columns exist.
     * Pool and database errors propagate.
     */
    template <typename JunctionEntity, typename ParentID, typename ChildID>
    core::Task<bool> unlink(const ParentID& parent_id, const ChildID& child_id) {
        co_return co_await execute_with_tx([&](Transaction& tx) { return tx.template unlink<JunctionEntity>(parent_id, child_id); });
    }
};

template <typename T>
inline core::Task<void> HasOne<T>::load(SqlDatabaseClient& client) {
    co_await load(client.pool());
}

template <typename T>
inline core::Task<void> HasOne<T>::load(SqlDatabaseClient* client) {
    if (!client) throw std::runtime_error("HasOne::load: client is null");
    co_await load(*client);
}

template <typename T>
inline core::Task<void> HasMany<T>::load(SqlDatabaseClient& client) {
    co_await load(client.pool());
}

template <typename T>
inline core::Task<void> HasMany<T>::load(SqlDatabaseClient* client) {
    if (!client) throw std::runtime_error("HasMany::load: client is null");
    co_await load(*client);
}

template <typename T>
inline core::Task<void> HasOne<T>::set(SqlDatabaseClient& client, T child) {
    co_await set(client.pool(), std::move(child));
}

template <typename T>
inline core::Task<void> HasOne<T>::set(SqlDatabaseClient* client, T child) {
    if (!client) throw std::runtime_error("HasOne::set: client is null");
    co_await set(*client, std::move(child));
}

template <typename T>
inline core::Task<void> HasOne<T>::clear(SqlDatabaseClient& client) {
    co_await clear(client.pool());
}

template <typename T>
inline core::Task<void> HasOne<T>::clear(SqlDatabaseClient* client) {
    if (!client) throw std::runtime_error("HasOne::clear: client is null");
    co_await clear(*client);
}

template <typename T>
inline core::Task<void> HasMany<T>::add(SqlDatabaseClient& client, T item) {
    co_await add(client.pool(), std::move(item));
}

template <typename T>
inline core::Task<void> HasMany<T>::add(SqlDatabaseClient* client, T item) {
    if (!client) throw std::runtime_error("HasMany::add: client is null");
    co_await add(*client, std::move(item));
}

template <typename T>
template <typename Arg>
inline core::Task<bool> HasMany<T>::remove(SqlDatabaseClient& client, const Arg& arg) {
    co_return co_await remove(client.pool(), arg);
}

template <typename T>
template <typename Arg>
inline core::Task<bool> HasMany<T>::remove(SqlDatabaseClient* client, const Arg& arg) {
    if (!client) throw std::runtime_error("HasMany::remove: client is null");
    co_return co_await remove(*client, arg);
}

template <typename T>
inline core::Task<void> HasMany<T>::attach(SqlDatabaseClient& client, const T& item) {
    co_await attach(client.pool(), item);
}

template <typename T>
inline core::Task<void> HasMany<T>::attach(SqlDatabaseClient* client, const T& item) {
    if (!client) throw std::runtime_error("HasMany::attach: client is null");
    co_await attach(*client, item);
}

template <typename T>
template <typename Arg>
inline core::Task<bool> HasMany<T>::detach(SqlDatabaseClient& client, const Arg& arg) {
    co_return co_await detach(client.pool(), arg);
}

template <typename T>
template <typename Arg>
inline core::Task<bool> HasMany<T>::detach(SqlDatabaseClient* client, const Arg& arg) {
    if (!client) throw std::runtime_error("HasMany::detach: client is null");
    co_return co_await detach(*client, arg);
}

} // namespace aegon::data::orm::sql
