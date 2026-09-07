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

namespace aegon::data::orm::sql {

class SqlDatabaseClient {
    PerCoreConnectionPool& pool_;

public:
    explicit SqlDatabaseClient(PerCoreConnectionPool& pool) : pool_(pool) {}

    [[nodiscard]] PerCoreConnectionPool& pool() noexcept { return pool_; }

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
    core::Task<void> insert(const Entity& entity) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_await tx.insert(entity);
    }

    template <typename Entity>
    core::Task<int64_t> insert_get_id(Entity& entity) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.template insert_get_id<Entity>(entity);
    }

    template <typename Entity>
    core::Task<int64_t> insert_get_id(const Entity& entity) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.template insert_get_id<Entity>(entity);
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
        co_return co_await tx.update_entity(entity);
    }

    template <typename Entity, typename ID>
    core::Task<std::optional<Entity>> find_by_id(const ID& id) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.find_by_id<Entity>(id);
    }

    template <typename Entity, typename ID>
    core::Task<bool> delete_by_id(const ID& id) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.delete_by_id<Entity>(id);
    }

    template <typename Entity>
    core::Task<std::vector<Entity>> fetch_all(const SelectBuilder<Entity>& builder) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.fetch_all(builder);
    }

    template <typename Entity>
    core::Task<std::optional<Entity>> fetch_one(const SelectBuilder<Entity>& builder) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.fetch_one(builder);
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
