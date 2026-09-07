#pragma once
#include "core/Task.h"
#include "PerCoreConnectionPool.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>
#include <stdexcept>
#include <span>

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
 */
template <typename T>
class HasOne {
    std::optional<T> data_{std::nullopt};
    bool loaded_{false};
    std::string owner_key_;
    std::function<core::Task<std::optional<T>>(Connection&, const std::string&)> loader_;

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
};

/**
 * @brief Represents a 1:N or N:M relation to a collection of child entities T.
 * 
 * Supports both eager loading (via .include()) and lazy loading (via co_await .load()).
 */
template <typename T>
class HasMany {
    std::vector<T> data_;
    bool loaded_{false};
    std::string owner_key_;
    std::function<core::Task<std::vector<T>>(Connection&, const std::string&)> loader_;

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
};

template <typename T>
inline core::Task<void> HasOne<T>::load(PerCoreConnectionPool& pool) {
    if (loaded_) co_return;
    auto guard = pool.acquire();
    co_await load(*guard);
}

template <typename T>
inline core::Task<void> HasMany<T>::load(PerCoreConnectionPool& pool) {
    if (loaded_) co_return;
    auto guard = pool.acquire();
    co_await load(*guard);
}

} // namespace aegon::data::orm::sql
