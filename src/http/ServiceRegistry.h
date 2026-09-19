#pragma once

#include <atomic>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <type_traits>

namespace aegon::http {

namespace detail {

inline size_t next_service_type_id() noexcept {
    static std::atomic<size_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

template <typename CleanT>
inline size_t service_type_id_impl() noexcept {
    static const size_t id = next_service_type_id();
    return id;
}

template <typename T>
inline size_t service_type_id() noexcept {
    return service_type_id_impl<std::remove_cvref_t<T>>();
}

struct KeyedServiceKey {
    size_t type_id{0};
    std::string name{};

    bool operator==(const KeyedServiceKey& other) const noexcept {
        return type_id == other.type_id && name == other.name;
    }
};

struct KeyedLookup {
    size_t type_id{0};
    std::string_view name{};
};

struct KeyedServiceKeyHash {
    using is_transparent = void;

    size_t operator()(const KeyedServiceKey& k) const noexcept {
        return hash_impl(k.type_id, k.name);
    }
    size_t operator()(const KeyedLookup& k) const noexcept {
        return hash_impl(k.type_id, k.name);
    }

private:
    static size_t hash_impl(size_t type_id, std::string_view name) noexcept {
        size_t h1 = std::hash<size_t>{}(type_id);
        size_t h2 = std::hash<std::string_view>{}(name);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

struct KeyedServiceKeyEqual {
    using is_transparent = void;

    bool operator()(const KeyedServiceKey& a, const KeyedServiceKey& b) const noexcept {
        return a.type_id == b.type_id && a.name == b.name;
    }
    bool operator()(const KeyedServiceKey& a, const KeyedLookup& b) const noexcept {
        return a.type_id == b.type_id && a.name == b.name;
    }
    bool operator()(const KeyedLookup& a, const KeyedServiceKey& b) const noexcept {
        return a.type_id == b.type_id && a.name == b.name;
    }
};

} // namespace detail

/**
 * @brief High-performance, type-indexed service dependency injection container
 * with zero-lock hot-path lookups and keyed/named service support.
 *
 * During server startup, services can be registered (unkeyed or named).
 * Once the server runs, the registry is frozen (`freeze()`), turning all subsequent
 * lookups into lock-free operations with O(1) indexed array reads for unkeyed services.
 */
class ServiceRegistry {
    mutable std::shared_mutex mutex_;
    std::atomic<bool> is_frozen_{false};

    // Staging containers (used prior to freeze)
    std::unordered_map<size_t, std::shared_ptr<void>> unkeyed_services_;
    std::unordered_map<detail::KeyedServiceKey, std::shared_ptr<void>, 
                       detail::KeyedServiceKeyHash, detail::KeyedServiceKeyEqual> keyed_services_;

    // Frozen hot-path tables (lock-free, zero-allocation reads)
    std::vector<void*> frozen_raw_unkeyed_;
    std::vector<std::shared_ptr<void>> frozen_shared_unkeyed_;
    std::unordered_map<detail::KeyedServiceKey, void*, 
                       detail::KeyedServiceKeyHash, detail::KeyedServiceKeyEqual> frozen_keyed_raw_;
    std::unordered_map<detail::KeyedServiceKey, std::shared_ptr<void>, 
                       detail::KeyedServiceKeyHash, detail::KeyedServiceKeyEqual> frozen_keyed_shared_;

public:
    ServiceRegistry() = default;
    ~ServiceRegistry() = default;

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    ServiceRegistry(ServiceRegistry&& other) noexcept {
        std::unique_lock lock(other.mutex_);
        unkeyed_services_ = std::move(other.unkeyed_services_);
        keyed_services_ = std::move(other.keyed_services_);
        frozen_raw_unkeyed_ = std::move(other.frozen_raw_unkeyed_);
        frozen_shared_unkeyed_ = std::move(other.frozen_shared_unkeyed_);
        frozen_keyed_raw_ = std::move(other.frozen_keyed_raw_);
        frozen_keyed_shared_ = std::move(other.frozen_keyed_shared_);
        is_frozen_.store(other.is_frozen_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    ServiceRegistry& operator=(ServiceRegistry&& other) noexcept {
        if (this != &other) {
            std::unique_lock lock1(mutex_, std::defer_lock);
            std::unique_lock lock2(other.mutex_, std::defer_lock);
            std::lock(lock1, lock2);
            unkeyed_services_ = std::move(other.unkeyed_services_);
            keyed_services_ = std::move(other.keyed_services_);
            frozen_raw_unkeyed_ = std::move(other.frozen_raw_unkeyed_);
            frozen_shared_unkeyed_ = std::move(other.frozen_shared_unkeyed_);
            frozen_keyed_raw_ = std::move(other.frozen_keyed_raw_);
            frozen_keyed_shared_ = std::move(other.frozen_keyed_shared_);
            is_frozen_.store(other.is_frozen_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    /**
     * @brief Freezes the registry, activating the zero-lock hot path for all handler lookups.
     * Subsequent calls to register_service() will throw an exception.
     */
    void freeze() {
        std::unique_lock lock(mutex_);
        if (is_frozen_.load(std::memory_order_relaxed)) {
            return;
        }

        // 1. Build flat direct-indexed arrays for unkeyed services
        size_t max_id = 0;
        for (const auto& [id, ptr] : unkeyed_services_) {
            if (id > max_id) max_id = id;
        }
        if (!unkeyed_services_.empty()) {
            frozen_raw_unkeyed_.assign(max_id + 1, nullptr);
            frozen_shared_unkeyed_.assign(max_id + 1, nullptr);
            for (const auto& [id, ptr] : unkeyed_services_) {
                frozen_raw_unkeyed_[id] = ptr.get();
                frozen_shared_unkeyed_[id] = ptr;
            }
        }

        // 2. Pre-populate frozen keyed maps for lock-free lookups
        frozen_keyed_raw_.clear();
        frozen_keyed_shared_.clear();
        for (const auto& [key, ptr] : keyed_services_) {
            frozen_keyed_raw_[key] = ptr.get();
            frozen_keyed_shared_[key] = ptr;
        }

        is_frozen_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool is_frozen() const noexcept {
        return is_frozen_.load(std::memory_order_acquire);
    }

    /**
     * @brief Registers an unkeyed service instance managed via std::shared_ptr.
     */
    template <typename T>
    void register_service(std::shared_ptr<T> service) {
        std::unique_lock lock(mutex_);
        if (is_frozen_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("ServiceRegistry: cannot register services after registry has been frozen.");
        }
        const size_t id = detail::service_type_id<T>();
        unkeyed_services_[id] = std::static_pointer_cast<void>(std::move(service));
    }

    /**
     * @brief Registers a named/keyed service instance managed via std::shared_ptr.
     */
    template <typename T>
    void register_service(std::string_view name, std::shared_ptr<T> service) {
        if (name.empty()) {
            register_service<T>(std::move(service));
            return;
        }
        std::unique_lock lock(mutex_);
        if (is_frozen_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("ServiceRegistry: cannot register services after registry has been frozen.");
        }
        const size_t id = detail::service_type_id<T>();
        detail::KeyedServiceKey key{id, std::string(name)};
        keyed_services_[key] = std::static_pointer_cast<void>(std::move(service));
    }

    /**
     * @brief Checks if a service of type T (and optional name) is registered.
     */
    template <typename T>
    [[nodiscard]] bool has(std::string_view name = "") const noexcept {
        return get<T>(name) != nullptr;
    }

    /**
     * @brief Retrieves a raw pointer to a service of type T (and optional name),
     * or nullptr if not registered.
     * When frozen, this executes without taking any mutex locks.
     */
    template <typename T>
    [[nodiscard]] T* get(std::string_view name = "") const noexcept {
        const size_t id = detail::service_type_id<T>();

        // Zero-Lock Hot Path when frozen
        if (is_frozen_.load(std::memory_order_acquire)) {
            if (name.empty()) {
                if (id < frozen_raw_unkeyed_.size()) {
                    return static_cast<T*>(frozen_raw_unkeyed_[id]);
                }
                return nullptr;
            }
            auto it = frozen_keyed_raw_.find(detail::KeyedLookup{id, name});
            if (it != frozen_keyed_raw_.end()) {
                return static_cast<T*>(it->second);
            }
            return nullptr;
        }

        // Fallback prior to freeze (setup / bootstrapping)
        std::shared_lock lock(mutex_);
        if (name.empty()) {
            auto it = unkeyed_services_.find(id);
            if (it != unkeyed_services_.end()) {
                return static_cast<T*>(it->second.get());
            }
            return nullptr;
        }
        auto it = keyed_services_.find(detail::KeyedLookup{id, name});
        if (it != keyed_services_.end()) {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    /**
     * @brief Retrieves a shared_ptr to a service of type T (and optional name),
     * or nullptr if not registered.
     * When frozen, this executes without taking any mutex locks.
     */
    template <typename T>
    [[nodiscard]] std::shared_ptr<T> get_shared(std::string_view name = "") const noexcept {
        const size_t id = detail::service_type_id<T>();

        // Zero-Lock Hot Path when frozen
        if (is_frozen_.load(std::memory_order_acquire)) {
            if (name.empty()) {
                if (id < frozen_shared_unkeyed_.size()) {
                    return std::static_pointer_cast<T>(frozen_shared_unkeyed_[id]);
                }
                return nullptr;
            }
            auto it = frozen_keyed_shared_.find(detail::KeyedLookup{id, name});
            if (it != frozen_keyed_shared_.end()) {
                return std::static_pointer_cast<T>(it->second);
            }
            return nullptr;
        }

        // Fallback prior to freeze
        std::shared_lock lock(mutex_);
        if (name.empty()) {
            auto it = unkeyed_services_.find(id);
            if (it != unkeyed_services_.end()) {
                return std::static_pointer_cast<T>(it->second);
            }
            return nullptr;
        }
        auto it = keyed_services_.find(detail::KeyedLookup{id, name});
        if (it != keyed_services_.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }

    /**
     * @brief Retrieves a reference to service T (and optional name),
     * throwing std::runtime_error if not found.
     */
    template <typename T>
    [[nodiscard]] T& require(std::string_view name = "") const {
        auto* ptr = get<T>(name);
        if (!ptr) {
            if (name.empty()) {
                throw std::runtime_error(std::string("ServiceRegistry: service of type '") + 
                                         typeid(T).name() + "' is not registered.");
            }
            throw std::runtime_error(std::string("ServiceRegistry: named service '") + 
                                     std::string(name) + "' of type '" + 
                                     typeid(T).name() + "' is not registered.");
        }
        return *ptr;
    }
};

} // namespace aegon::http
