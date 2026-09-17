#pragma once

#include "PerCoreConnectionPool.h"
#include "Transaction.h"
#include "SelectBuilder.h"
#include "InsertBuilder.h"
#include "UpdateBuilder.h"
#include "DeleteBuilder.h"
#include "core/Task.h"
#include <concepts>
#include <functional>
#include <optional>
#include <vector>
#include <exception>

#include "data/cache/CacheBackend.h"

namespace aegon::data::orm::sql {

class SqlDatabaseClient {
    PerCoreConnectionPool& pool_;
    std::shared_ptr<cache::CacheBackend> cache_{nullptr};

public:
    explicit SqlDatabaseClient(PerCoreConnectionPool& pool) : pool_(pool) {}

    [[nodiscard]] PerCoreConnectionPool& pool() noexcept { return pool_; }

    void set_cache(std::shared_ptr<cache::CacheBackend> cache) noexcept {
        cache_ = std::move(cache);
    }

    [[nodiscard]] std::shared_ptr<cache::CacheBackend> cache() const noexcept {
        return cache_;
    }

    // Option 4: Transaction & Unit of Work Lifecycle
    template <typename Func>
    core::Task<void> transaction(Func&& block) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
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
        }
    }

    // Direct non-transactional single-operation conveniences
    template <typename Entity>
    core::Task<void> insert(Entity& entity) {
        auto schema = Entity::schema();
        if (schema.has_auto_increment_pk()) {
            co_await insert_get_id(entity);
            co_return;
        }

        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_await tx.insert(entity);

        if (cache_ && schema.is_cached()) {
            if (schema.cache_config().invalidation == InvalidationMode::Partitioned && schema.cache_config().partition_extractor) {
                std::string part_key = schema.table_name() + ":part:" + schema.cache_config().partition_extractor(entity) + ":epoch";
                co_await cache_->incr(part_key);
            } else if (schema.cache_config().invalidation == InvalidationMode::StrictEpoch) {
                co_await cache_->incr(schema.table_name() + ":epoch");
            }

            auto pk_val = schema.extract_values(entity, false);
            for (const auto& [col, val] : pk_val) {
                if (col == schema.primary_key_name() && !val.empty()) {
                    std::string id_key = schema.table_name() + ":id:" + val;
                    co_await cache_->set(id_key, schema.serialize_entity_json(entity), schema.cache_config().ttl);
                    break;
                }
            }
        }
    }

    template <typename Entity>
    core::Task<void> insert(const Entity& entity) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_await tx.insert(entity);

        auto schema = Entity::schema();
        if (cache_ && schema.is_cached()) {
            // Epoch invalidation
            if (schema.cache_config().invalidation == InvalidationMode::Partitioned && schema.cache_config().partition_extractor) {
                std::string part_key = schema.table_name() + ":part:" + schema.cache_config().partition_extractor(entity) + ":epoch";
                co_await cache_->incr(part_key);
            }
            if (schema.cache_config().invalidation != InvalidationMode::TtlOnly) {
                co_await cache_->incr(schema.table_name() + ":epoch");
            }

            // Cache entity by ID if available
            auto pk_val = schema.extract_values(entity, false);
            for (const auto& [col, val] : pk_val) {
                if (col == schema.primary_key_name() && !val.empty()) {
                    std::string id_key = schema.table_name() + ":id:" + val;
                    co_await cache_->set(id_key, schema.serialize_entity_json(entity), schema.cache_config().ttl);
                    break;
                }
            }
        }
    }

    template <typename Entity>
    core::Task<int64_t> insert_get_id(Entity& entity) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        int64_t id = co_await tx.template insert_get_id<Entity>(entity);

        auto schema = Entity::schema();
        if (cache_ && schema.is_cached()) {
            if (schema.cache_config().invalidation == InvalidationMode::Partitioned && schema.cache_config().partition_extractor) {
                std::string part_key = schema.table_name() + ":part:" + schema.cache_config().partition_extractor(entity) + ":epoch";
                co_await cache_->incr(part_key);
            }
            if (schema.cache_config().invalidation != InvalidationMode::TtlOnly) {
                co_await cache_->incr(schema.table_name() + ":epoch");
            }

            std::string id_key = schema.table_name() + ":id:" + std::to_string(id);
            co_await cache_->set(id_key, schema.serialize_entity_json(entity), schema.cache_config().ttl);
        }

        co_return id;
    }

    template <typename Entity>
    core::Task<int64_t> insert_get_id(const Entity& entity) {
        Entity copy = entity;
        co_return co_await insert_get_id<Entity>(copy);
    }

    template <typename Entity>
    core::Task<void> insert_tree(Entity& entity) {
        co_await transaction([&](Transaction& tx) -> core::Task<void> {
            co_await tx.insert_tree(entity);
        });
    }

    template <typename Entity>
    core::Task<size_t> update_entity(const Entity& entity) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        size_t n = co_await tx.update_entity(entity);

        auto schema = Entity::schema();
        if (cache_ && schema.is_cached()) {
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
                    co_await cache_->set(id_key, schema.serialize_entity_json(entity), schema.cache_config().ttl);
                } else {
                    co_await cache_->del(id_key);
                }
            }

            if (schema.cache_config().unique_column && schema.cache_config().unique_extractor) {
                std::string u_key = schema.table_name() + ":" + *schema.cache_config().unique_column + ":" + schema.cache_config().unique_extractor(entity);
                co_await cache_->del(u_key);
            }

            if (schema.cache_config().invalidation == InvalidationMode::Partitioned && schema.cache_config().partition_extractor) {
                std::string part_key = schema.table_name() + ":part:" + schema.cache_config().partition_extractor(entity) + ":epoch";
                co_await cache_->incr(part_key);
            }
            if (schema.cache_config().invalidation != InvalidationMode::TtlOnly) {
                co_await cache_->incr(schema.table_name() + ":epoch");
            }
        }

        co_return n;
    }

    template <typename Entity, typename ID>
    core::Task<std::optional<Entity>> find_by_id(const ID& id) {
        auto schema = Entity::schema();
        std::string id_str = format_param_value(id);
        std::string id_key = schema.table_name() + ":id:" + id_str;

        if (cache_ && schema.is_cached() && schema.cache_config().by_id) {
            auto cached_json = co_await cache_->get(id_key);
            if (cached_json.has_value()) {
                Entity e{};
                if (schema.deserialize_entity_json(*cached_json, e)) {
                    co_return e;
                }
            }
        }

        auto guard = pool_.acquire();
        Transaction tx(*guard);
        auto opt_entity = co_await tx.find_by_id<Entity>(id);

        if (opt_entity && cache_ && schema.is_cached() && schema.cache_config().by_id) {
            std::string json = schema.serialize_entity_json(*opt_entity);
            co_await cache_->set(id_key, json, schema.cache_config().ttl);
            if (schema.cache_config().unique_column && schema.cache_config().unique_extractor) {
                std::string u_key = schema.table_name() + ":" + *schema.cache_config().unique_column + ":" + schema.cache_config().unique_extractor(*opt_entity);
                co_await cache_->set(u_key, id_str, schema.cache_config().ttl);
            }
        }

        co_return opt_entity;
    }

    template <typename Entity, typename FieldType, typename ValueType>
    core::Task<std::optional<Entity>> find_by_unique(FieldType Entity::* field, const ValueType& val) {
        auto schema = Entity::schema();
        std::string col_name = schema.resolve_column_name(field);
        std::string val_str = format_param_value(val);

        if (cache_ && schema.is_cached() && schema.cache_config().unique_column && *schema.cache_config().unique_column == col_name) {
            std::string u_key = schema.table_name() + ":" + col_name + ":" + val_str;
            auto opt_pk = co_await cache_->get(u_key);
            if (opt_pk) {
                auto by_id_res = co_await find_by_id<Entity>(*opt_pk);
                if (by_id_res) co_return by_id_res;
            }
        }

        auto guard = pool_.acquire();
        Transaction tx(*guard);
        auto q = from<Entity>().where(field, Op::Eq, val);
        auto opt_entity = co_await tx.fetch_one(q);
        if (opt_entity && cache_ && schema.is_cached()) {
            auto vals = schema.extract_values(*opt_entity, false);
            std::string pk;
            for (const auto& [c, v] : vals) {
                if (c == schema.primary_key_name()) {
                    pk = v;
                    break;
                }
            }
            if (!pk.empty()) {
                std::string id_key = schema.table_name() + ":id:" + pk;
                co_await cache_->set(id_key, schema.serialize_entity_json(*opt_entity), schema.cache_config().ttl);
                std::string u_key = schema.table_name() + ":" + col_name + ":" + val_str;
                co_await cache_->set(u_key, pk, schema.cache_config().ttl);
            }
        }
        co_return opt_entity;
    }

    template <typename Entity, typename ID>
    core::Task<bool> delete_by_id(const ID& id) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        bool ok = co_await tx.delete_by_id<Entity>(id);

        auto schema = Entity::schema();
        if (ok && cache_ && schema.is_cached()) {
            std::string id_str = format_param_value(id);
            std::string id_key = schema.table_name() + ":id:" + id_str;
            co_await cache_->del(id_key);

            if (schema.cache_config().invalidation != InvalidationMode::TtlOnly) {
                co_await cache_->incr(schema.table_name() + ":epoch");
            }
        }

        co_return ok;
    }

    template <typename Entity>
    core::Task<std::vector<Entity>> fetch_all(const SelectBuilder<Entity>& builder) {
        auto schema = builder.schema();
        bool use_cache = (cache_ != nullptr) && builder.is_cached();

        std::string query_key;
        if (use_cache) {
            auto guard = pool_.acquire();
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
                        auto guard = pool_.acquire();
                        auto dialect = guard->dialect();
                        co_await builder.eager_load_includes(entities, *guard, dialect);
                    }
                    co_return entities;
                }
            }
        }

        auto guard = pool_.acquire();
        Transaction tx(*guard);
        auto entities = co_await tx.fetch_all(builder);

        if (use_cache && !query_key.empty()) {
            std::string ids_str;
            auto ttl = builder.cache_ttl().value_or(std::chrono::seconds(300));
            for (size_t i = 0; i < entities.size(); ++i) {
                auto vals = schema.extract_values(entities[i], false);
                std::string pk;
                for (const auto& [col, val] : vals) {
                    if (col == schema.primary_key_name()) {
                        pk = val;
                        break;
                    }
                }
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

    template <typename Entity>
    core::Task<uint64_t> count(const SelectBuilder<Entity>& builder) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.count(builder);
    }

    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> sum(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.sum(builder, field);
    }

    template <typename ResultType, typename Entity, typename FieldType>
    core::Task<std::optional<ResultType>> sum(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.template sum<ResultType>(builder, field);
    }

    template <typename Entity, typename FieldType>
    core::Task<std::optional<double>> avg(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.avg(builder, field);
    }

    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> min(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.min(builder, field);
    }

    template <typename Entity, typename FieldType>
    core::Task<std::optional<unwrapped_type_t<FieldType>>> max(const SelectBuilder<Entity>& builder, FieldType Entity::* field) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.max(builder, field);
    }

    template <typename Entity>
    core::Task<Page<Entity>> paginate(SelectBuilder<Entity> builder, size_t page = 1, size_t per_page = 20) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.paginate(std::move(builder), page, per_page);
    }

    core::Task<size_t> execute(const QueryResult& query) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.execute(query);
    }

    core::Task<size_t> execute(std::string_view sql, const std::vector<std::string>& params = {}) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.execute(sql, params);
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

    template <typename JunctionEntity, typename ParentID, typename ChildID>
    core::Task<void> link(const ParentID& parent_id, const ChildID& child_id) {
        auto guard = pool_.acquire();
        auto dialect = guard->dialect();
        auto schema = JunctionEntity::schema();
        std::string sql = "INSERT INTO ";
        sql.append(DialectTraits::quote_identifier(dialect, schema.table_name()));
        sql.append(" (");
        sql.append(DialectTraits::quote_identifier(dialect, schema.columns()[0].column_name));
        sql.append(", ");
        sql.append(DialectTraits::quote_identifier(dialect, schema.columns()[1].column_name));
        sql.append(") VALUES (");
        std::string p1, p2;
        DialectTraits::format_placeholder(dialect, 1, p1);
        DialectTraits::format_placeholder(dialect, 2, p2);
        sql.append(p1);
        sql.append(", ");
        sql.append(p2);
        sql.append(");");

        co_await guard->execute(sql, {format_param_value(parent_id), format_param_value(child_id)});
    }

    template <typename JunctionEntity, typename ParentID, typename ChildID>
    core::Task<bool> unlink(const ParentID& parent_id, const ChildID& child_id) {
        auto guard = pool_.acquire();
        auto dialect = guard->dialect();
        auto schema = JunctionEntity::schema();
        std::string sql = "DELETE FROM ";
        sql.append(DialectTraits::quote_identifier(dialect, schema.table_name()));
        sql.append(" WHERE ");
        sql.append(DialectTraits::quote_identifier(dialect, schema.columns()[0].column_name));
        sql.append(" = ");
        std::string p1;
        DialectTraits::format_placeholder(dialect, 1, p1);
        sql.append(p1);
        sql.append(" AND ");
        sql.append(DialectTraits::quote_identifier(dialect, schema.columns()[1].column_name));
        sql.append(" = ");
        std::string p2;
        DialectTraits::format_placeholder(dialect, 2, p2);
        sql.append(p2);
        sql.append(";");

        size_t n = co_await guard->execute(sql, {format_param_value(parent_id), format_param_value(child_id)});
        co_return n > 0;
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
