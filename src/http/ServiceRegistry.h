#pragma once

#include <unordered_map>
#include <typeindex>
#include <typeinfo>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>

namespace aegon::http {

/**
 * @brief High-performance, type-indexed service dependency injection container.
 *
 * Allows arbitrary services, database clients, caches, and custom application state
 * to be registered on the Server and accessed safely inside route handlers via Context.
 */
class ServiceRegistry {
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;

public:
    ServiceRegistry() = default;
    ~ServiceRegistry() = default;

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    ServiceRegistry(ServiceRegistry&& other) noexcept {
        std::unique_lock lock(other.mutex_);
        services_ = std::move(other.services_);
    }

    ServiceRegistry& operator=(ServiceRegistry&& other) noexcept {
        if (this != &other) {
            std::unique_lock lock1(mutex_, std::defer_lock);
            std::unique_lock lock2(other.mutex_, std::defer_lock);
            std::lock(lock1, lock2);
            services_ = std::move(other.services_);
        }
        return *this;
    }

    /**
     * @brief Registers a service instance managed via std::shared_ptr.
     */
    template <typename T>
    void register_service(std::shared_ptr<T> service) {
        std::unique_lock lock(mutex_);
        services_[std::type_index(typeid(T))] = std::static_pointer_cast<void>(std::move(service));
    }

    /**
     * @brief Checks if a service of type T is registered.
     */
    template <typename T>
    [[nodiscard]] bool has() const noexcept {
        std::shared_lock lock(mutex_);
        return services_.find(std::type_index(typeid(T))) != services_.end();
    }

    /**
     * @brief Retrieves a pointer to a service of type T, or nullptr if not registered.
     */
    template <typename T>
    [[nodiscard]] T* get() const noexcept {
        std::shared_lock lock(mutex_);
        auto it = services_.find(std::type_index(typeid(T)));
        if (it != services_.end()) {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    /**
     * @brief Retrieves a shared_ptr to a service of type T, or nullptr if not registered.
     */
    template <typename T>
    [[nodiscard]] std::shared_ptr<T> get_shared() const noexcept {
        std::shared_lock lock(mutex_);
        auto it = services_.find(std::type_index(typeid(T)));
        if (it != services_.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }

    /**
     * @brief Retrieves a reference to service T, throwing std::runtime_error if not found.
     */
    template <typename T>
    [[nodiscard]] T& require() const {
        auto* ptr = get<T>();
        if (!ptr) {
            throw std::runtime_error(std::string("ServiceRegistry: service of type '") + 
                                     typeid(T).name() + "' is not registered.");
        }
        return *ptr;
    }
};

} // namespace aegon::http
