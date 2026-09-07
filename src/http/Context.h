#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "data/orm/sql/SqlDatabaseClient.h"
#include <stdexcept>
#include <utility>

namespace aegon::http {

class SqlAccessor {
    data::orm::sql::SqlDatabaseClient* client_{nullptr};

public:
    constexpr SqlAccessor() noexcept = default;
    explicit SqlAccessor(data::orm::sql::SqlDatabaseClient* c) noexcept : client_(c) {}

    [[nodiscard]] data::orm::sql::SqlDatabaseClient* operator->() const {
        if (!client_) throw std::runtime_error("ctx.db.sql: database client not configured.");
        return client_;
    }

    [[nodiscard]] data::orm::sql::SqlDatabaseClient& operator*() const {
        if (!client_) throw std::runtime_error("ctx.db.sql: database client not configured.");
        return *client_;
    }

    [[nodiscard]] operator data::orm::sql::SqlDatabaseClient&() const {
        if (!client_) throw std::runtime_error("ctx.db.sql: database client not configured.");
        return *client_;
    }

    [[nodiscard]] bool is_configured() const noexcept {
        return client_ != nullptr;
    }

    template <typename Func>
    auto transaction(Func&& block) {
        return (*this)->transaction(std::forward<Func>(block));
    }

    template <typename Entity>
    auto insert(const Entity& entity) {
        return (*this)->insert(entity);
    }

    template <typename Entity>
    auto update_entity(const Entity& entity) {
        return (*this)->update_entity(entity);
    }

    template <typename Entity, typename ID>
    auto find_by_id(const ID& id) {
        return (*this)->template find_by_id<Entity>(id);
    }

    template <typename Entity, typename ID>
    auto delete_by_id(const ID& id) {
        return (*this)->template delete_by_id<Entity>(id);
    }

    template <typename Entity>
    auto fetch_all(const data::orm::sql::SelectBuilder<Entity>& builder) {
        return (*this)->fetch_all(builder);
    }

    template <typename Entity>
    auto fetch_one(const data::orm::sql::SelectBuilder<Entity>& builder) {
        return (*this)->fetch_one(builder);
    }

    auto execute(const data::orm::sql::QueryResult& query) {
        return (*this)->execute(query);
    }

    auto execute(std::string_view sql, const std::vector<std::string>& params = {}) {
        return (*this)->execute(sql, params);
    }

    template <typename Entity>
    auto from() {
        return (*this)->template from<Entity>();
    }

    template <typename Entity>
    auto update() {
        return (*this)->template update<Entity>();
    }

    template <typename Entity>
    auto delete_from() {
        return (*this)->template delete_from<Entity>();
    }
};

struct DatabaseContext {
    SqlAccessor sql;
    // Future: MongoAccessor mongo;
};

/**
 * @brief Zero-overhead compile-time Context passed to all route handlers.
 *
 * Provides direct access to inbound request data, response builder, route parameters,
 * multi-database client (ctx.db.sql), SIMD UUID extraction, and dependency injection.
 */
class Context {
private:
    Request& req_;
    Response& res_;
    void* user_state_{nullptr};

public:
    // Multi-database namespace
    DatabaseContext db;

    Context(Request& req, Response& res, void* user_state = nullptr, data::orm::sql::SqlDatabaseClient* sql_client = nullptr) noexcept
        : req_(req), res_(res), user_state_(user_state), db{SqlAccessor(sql_client)} {}

    // Core accessors
    [[nodiscard]] Request& req() noexcept { return req_; }
    [[nodiscard]] const Request& req() const noexcept { return req_; }

    [[nodiscard]] Response& res() noexcept { return res_; }
    [[nodiscard]] const Response& res() const noexcept { return res_; }

    /**
     * @brief Binds inbound JSON body into typed DTO T with automatic validation & 422 error response.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> bind_json() {
        return req_.bind_json<T>(res_);
    }

    /**
     * @brief Binds URL query string into typed DTO T with automatic validation & 422 error response.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> bind_query() {
        return req_.bind_query<T>(res_);
    }

    /**
     * @brief Binds route parameters into typed DTO T with automatic validation & 422 error response.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> bind_path() {
        return req_.bind_path<T>(res_);
    }

    // Typed user state / dependency injection
    template <typename T>
    [[nodiscard]] T* state() noexcept {
        return static_cast<T*>(user_state_);
    }

    template <typename T>
    [[nodiscard]] const T* state() const noexcept {
        return static_cast<const T*>(user_state_);
    }
};

} // namespace aegon::http
