#pragma once
#include "core/Task.h"
#include "PerCoreConnectionPool.h"
#include "Expression.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>
#include <stdexcept>
#include <span>
#include <algorithm>

namespace aegon::data::orm::sql {

class SqlDatabaseClient;

enum class RelationKind {
    OneToOne,
    OneToMany,
    ManyToMany
};

/**
 * @brief Represents a 1:1 relation to a child entity T.
 * 
 * Supports both eager loading (via .include()) and lazy loading (via co_await .load()).
 * Also supports active relational mutators: .set() and .clear().
 */
template <typename T>
class HasOne {
    std::optional<T> data_{std::nullopt};
    bool loaded_{false};
    std::string owner_key_;
    std::function<core::Task<std::optional<T>>(Connection&, const std::string&)> loader_;
    std::function<core::Task<void>(Connection&, const std::string&, T&)> setter_;
    std::function<core::Task<void>(Connection&, const std::string&)> clearer_;

public:
    using value_type = T;

    HasOne() = default;
    HasOne(const HasOne&) = default;
    HasOne(HasOne&&) noexcept = default;
    HasOne& operator=(const HasOne&) = default;
    HasOne& operator=(HasOne&&) noexcept = default;

    explicit HasOne(T val) : data_(std::move(val)), loaded_(true) {}

    void set_loader(std::string owner_key,
                    std::function<core::Task<std::optional<T>>(Connection&, const std::string&)> loader) {
        owner_key_ = std::move(owner_key);
        loader_ = std::move(loader);
    }

    void set_owner_key(std::string owner_key) {
        owner_key_ = std::move(owner_key);
    }

    void set_mutators(
        std::function<core::Task<void>(Connection&, const std::string&, T&)> setter,
        std::function<core::Task<void>(Connection&, const std::string&)> clearer) {
        setter_ = std::move(setter);
        clearer_ = std::move(clearer);
    }

    void set_value(T val) {
        data_ = std::move(val);
        loaded_ = true;
    }

    void set_null() {
        data_ = std::nullopt;
        loaded_ = true;
    }

    void reset() {
        data_ = std::nullopt;
        loaded_ = false;
    }

    template <typename... Args>
    T& emplace(Args&&... args) {
        data_.emplace(std::forward<Args>(args)...);
        loaded_ = true;
        return *data_;
    }

    void set_loaded(bool l = true) noexcept { loaded_ = l; }
    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }
    [[nodiscard]] bool has_value() const noexcept { return data_.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const T& value() const {
        if (!data_) throw std::runtime_error("HasOne: value is empty / nullopt");
        return *data_;
    }
    [[nodiscard]] T& value() {
        if (!data_) throw std::runtime_error("HasOne: value is empty / nullopt");
        return *data_;
    }

    [[nodiscard]] const T* operator->() const { return &value(); }
    [[nodiscard]] T* operator->() { return &value(); }

    [[nodiscard]] const T& operator*() const { return value(); }
    [[nodiscard]] T& operator*() { return value(); }

    [[nodiscard]] const std::optional<T>& get() const noexcept { return data_; }
    [[nodiscard]] std::optional<T>& get() noexcept { return data_; }

    [[nodiscard]] const std::string& owner_key() const noexcept { return owner_key_; }

    core::Task<void> load(Connection& conn) {
        if (loaded_) co_return;
        if (!loader_) throw std::runtime_error("HasOne: no lazy loader configured");
        data_ = co_await loader_(conn, owner_key_);
        loaded_ = true;
    }

    core::Task<void> load(PerCoreConnectionPool& pool);
    core::Task<void> load(SqlDatabaseClient& client);
    core::Task<void> load(SqlDatabaseClient* client);

    core::Task<void> set(Connection& conn, T child) {
        if (!setter_) throw std::runtime_error("HasOne: no setter mutator configured");
        co_await setter_(conn, owner_key_, child);
        data_ = std::move(child);
        loaded_ = true;
    }

    core::Task<void> set(PerCoreConnectionPool& pool, T child);
    core::Task<void> set(SqlDatabaseClient& client, T child);
    core::Task<void> set(SqlDatabaseClient* client, T child);

    core::Task<void> clear(Connection& conn) {
        if (!clearer_) throw std::runtime_error("HasOne: no clearer mutator configured");
        co_await clearer_(conn, owner_key_);
        data_ = std::nullopt;
        loaded_ = true;
    }

    core::Task<void> clear(PerCoreConnectionPool& pool);
    core::Task<void> clear(SqlDatabaseClient& client);
    core::Task<void> clear(SqlDatabaseClient* client);
};

/**
 * @brief Represents a 1:N or N:M relation to a collection of child entities T.
 * 
 * Supports both eager loading (via .include()) and lazy loading (via co_await .load()).
 * Also supports active relational mutators: .add(), .remove(), .attach(), and .detach().
 */
template <typename T>
class HasMany {
    std::vector<T> data_;
    bool loaded_{false};
    std::string owner_key_;
    std::function<core::Task<std::vector<T>>(Connection&, const std::string&)> loader_;
    std::function<core::Task<int64_t>(Connection&, const std::string&, T&)> adder_;
    std::function<core::Task<bool>(Connection&, const std::string&, const std::string&, std::vector<T>&)> remover_;
    std::function<core::Task<void>(Connection&, const std::string&, const T&, std::vector<T>&)> attacher_;
    std::function<core::Task<bool>(Connection&, const std::string&, const std::string&, std::vector<T>&)> detacher_;

public:
    using value_type = T;

    HasMany() = default;
    HasMany(const HasMany&) = default;
    HasMany(HasMany&&) noexcept = default;
    HasMany& operator=(const HasMany&) = default;
    HasMany& operator=(HasMany&&) noexcept = default;

    explicit HasMany(std::vector<T> items) : data_(std::move(items)), loaded_(true) {}

    void set_loader(std::string owner_key,
                    std::function<core::Task<std::vector<T>>(Connection&, const std::string&)> loader) {
        owner_key_ = std::move(owner_key);
        loader_ = std::move(loader);
    }

    void set_owner_key(std::string owner_key) {
        owner_key_ = std::move(owner_key);
    }

    void set_one_to_many_mutators(
        std::function<core::Task<int64_t>(Connection&, const std::string&, T&)> adder,
        std::function<core::Task<bool>(Connection&, const std::string&, const std::string&, std::vector<T>&)> remover) {
        adder_ = std::move(adder);
        remover_ = std::move(remover);
    }

    void set_many_to_many_mutators(
        std::function<core::Task<void>(Connection&, const std::string&, const T&, std::vector<T>&)> attacher,
        std::function<core::Task<bool>(Connection&, const std::string&, const std::string&, std::vector<T>&)> detacher) {
        attacher_ = std::move(attacher);
        detacher_ = std::move(detacher);
    }

    void set_value(std::vector<T> val) {
        data_ = std::move(val);
        loaded_ = true;
    }

    void push_back(T item) {
        data_.push_back(std::move(item));
    }

    void clear() {
        data_.clear();
        loaded_ = false;
    }

    void set_loaded(bool l = true) noexcept { loaded_ = l; }
    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }
    [[nodiscard]] size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    [[nodiscard]] const T& operator[](size_t idx) const { return data_[idx]; }
    [[nodiscard]] T& operator[](size_t idx) { return data_[idx]; }

    [[nodiscard]] const T& front() const { return data_.front(); }
    [[nodiscard]] T& front() { return data_.front(); }
    [[nodiscard]] const T& back() const { return data_.back(); }
    [[nodiscard]] T& back() { return data_.back(); }

    auto begin() noexcept { return data_.begin(); }
    auto end() noexcept { return data_.end(); }
    auto begin() const noexcept { return data_.begin(); }
    auto end() const noexcept { return data_.end(); }
    auto cbegin() const noexcept { return data_.cbegin(); }
    auto cend() const noexcept { return data_.cend(); }

    [[nodiscard]] const std::vector<T>& get() const noexcept { return data_; }
    [[nodiscard]] std::vector<T>& get() noexcept { return data_; }

    [[nodiscard]] const std::string& owner_key() const noexcept { return owner_key_; }

    core::Task<void> load(Connection& conn) {
        if (loaded_) co_return;
        if (!loader_) throw std::runtime_error("HasMany: no lazy loader configured");
        data_ = co_await loader_(conn, owner_key_);
        loaded_ = true;
    }

    core::Task<void> load(PerCoreConnectionPool& pool);
    core::Task<void> load(SqlDatabaseClient& client);
    core::Task<void> load(SqlDatabaseClient* client);

    // 1:N Mutators
    core::Task<void> add(Connection& conn, T item) {
        if (!adder_) throw std::runtime_error("HasMany: no adder mutator configured (is this a 1:N relation?)");
        co_await adder_(conn, owner_key_, item);
        data_.push_back(std::move(item));
        loaded_ = true;
    }

    core::Task<void> add(PerCoreConnectionPool& pool, T item);
    core::Task<void> add(SqlDatabaseClient& client, T item);
    core::Task<void> add(SqlDatabaseClient* client, T item);

    template <typename ID>
        requires (!std::is_same_v<std::decay_t<ID>, T>)
    core::Task<bool> remove(Connection& conn, const ID& item_id) {
        if (!remover_) throw std::runtime_error("HasMany: no remover mutator configured (is this a 1:N relation?)");
        co_return co_await remover_(conn, owner_key_, format_param_value(item_id), data_);
    }

    core::Task<bool> remove(Connection& conn, const T& item) {
        if (!remover_) throw std::runtime_error("HasMany: no remover mutator configured (is this a 1:N relation?)");
        std::string id_str;
        if constexpr (requires { T::schema(); }) {
            auto s = T::schema();
            for (const auto& [col, val] : s.extract_values(item, false)) {
                if (col == s.primary_key_name()) {
                    id_str = val;
                    break;
                }
            }
        }
        co_return co_await remover_(conn, owner_key_, id_str, data_);
    }

    template <typename Arg>
    core::Task<bool> remove(PerCoreConnectionPool& pool, const Arg& arg);
    template <typename Arg>
    core::Task<bool> remove(SqlDatabaseClient& client, const Arg& arg);
    template <typename Arg>
    core::Task<bool> remove(SqlDatabaseClient* client, const Arg& arg);

    // N:M Mutators
    core::Task<void> attach(Connection& conn, const T& item) {
        if (!attacher_) throw std::runtime_error("HasMany: no attacher mutator configured (is this an N:M relation?)");
        co_await attacher_(conn, owner_key_, item, data_);
        loaded_ = true;
    }

    core::Task<void> attach(PerCoreConnectionPool& pool, const T& item);
    core::Task<void> attach(SqlDatabaseClient& client, const T& item);
    core::Task<void> attach(SqlDatabaseClient* client, const T& item);

    template <typename ID>
        requires (!std::is_same_v<std::decay_t<ID>, T>)
    core::Task<bool> detach(Connection& conn, const ID& item_id) {
        if (!detacher_) throw std::runtime_error("HasMany: no detacher mutator configured (is this an N:M relation?)");
        co_return co_await detacher_(conn, owner_key_, format_param_value(item_id), data_);
    }

    core::Task<bool> detach(Connection& conn, const T& item) {
        if (!detacher_) throw std::runtime_error("HasMany: no detacher mutator configured (is this an N:M relation?)");
        std::string id_str;
        if constexpr (requires { T::schema(); }) {
            auto s = T::schema();
            for (const auto& [col, val] : s.extract_values(item, false)) {
                if (col == s.primary_key_name()) {
                    id_str = val;
                    break;
                }
            }
        }
        co_return co_await detacher_(conn, owner_key_, id_str, data_);
    }

    template <typename Arg>
    core::Task<bool> detach(PerCoreConnectionPool& pool, const Arg& arg);
    template <typename Arg>
    core::Task<bool> detach(SqlDatabaseClient& client, const Arg& arg);
    template <typename Arg>
    core::Task<bool> detach(SqlDatabaseClient* client, const Arg& arg);
};

template <typename T>
inline core::Task<void> HasOne<T>::load(PerCoreConnectionPool& pool) {
    if (loaded_) co_return;
    auto guard = pool.acquire();
    co_await load(*guard);
}

template <typename T>
inline core::Task<void> HasOne<T>::set(PerCoreConnectionPool& pool, T child) {
    auto guard = pool.acquire();
    co_await set(*guard, std::move(child));
}

template <typename T>
inline core::Task<void> HasOne<T>::clear(PerCoreConnectionPool& pool) {
    auto guard = pool.acquire();
    co_await clear(*guard);
}

template <typename T>
inline core::Task<void> HasMany<T>::load(PerCoreConnectionPool& pool) {
    if (loaded_) co_return;
    auto guard = pool.acquire();
    co_await load(*guard);
}

template <typename T>
inline core::Task<void> HasMany<T>::add(PerCoreConnectionPool& pool, T item) {
    auto guard = pool.acquire();
    co_await add(*guard, std::move(item));
}

template <typename T>
template <typename Arg>
inline core::Task<bool> HasMany<T>::remove(PerCoreConnectionPool& pool, const Arg& arg) {
    auto guard = pool.acquire();
    co_return co_await remove(*guard, arg);
}

template <typename T>
inline core::Task<void> HasMany<T>::attach(PerCoreConnectionPool& pool, const T& item) {
    auto guard = pool.acquire();
    co_await attach(*guard, item);
}

template <typename T>
template <typename Arg>
inline core::Task<bool> HasMany<T>::detach(PerCoreConnectionPool& pool, const Arg& arg) {
    auto guard = pool.acquire();
    co_return co_await detach(*guard, arg);
}

} // namespace aegon::data::orm::sql
