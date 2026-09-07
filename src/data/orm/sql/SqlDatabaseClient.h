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
    core::Task<int64_t> insert_get_id(const Entity& entity) {
        auto guard = pool_.acquire();
        Transaction tx(*guard);
        co_return co_await tx.template insert_get_id<Entity>(entity);
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
};

} // namespace aegon::data::orm::sql
