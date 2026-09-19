#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "http/ServiceRegistry.h"
#include "http/ProblemDetails.h"
#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <memory>

namespace aegon::http {

/**
 * @brief Zero-overhead compile-time Context passed to route handlers.
 *
 * Provides direct access to inbound request data (req), outbound response builder (res),
 * DTO binders (bind_json, bind_query, bind_path), the type-safe ServiceRegistry,
 * and a per-request typed data bag (set, get, local, has) for middleware data passing.
 */
class Context {
private:
    Request& req_;
    Response& res_;
    const ServiceRegistry* services_{nullptr};
    std::unordered_map<std::type_index, std::shared_ptr<void>> local_store_;

public:
    Context(Request& req, Response& res, const ServiceRegistry* services = nullptr) noexcept
        : req_(req), res_(res), services_(services) {}

    /**
     * @brief Stores a typed value in the per-request data bag.
     */
    template <typename T>
    void set(T value) {
        local_store_[std::type_index(typeid(T))] = std::make_shared<T>(std::move(value));
    }

    /**
     * @brief Retrieves a pointer to the typed value in the per-request data bag, or nullptr if not present.
     */
    template <typename T>
    [[nodiscard]] T* get() noexcept {
        auto it = local_store_.find(std::type_index(typeid(T)));
        if (it == local_store_.end()) {
            return nullptr;
        }
        return static_cast<T*>(it->second.get());
    }

    template <typename T>
    [[nodiscard]] const T* get() const noexcept {
        auto it = local_store_.find(std::type_index(typeid(T)));
        if (it == local_store_.end()) {
            return nullptr;
        }
        return static_cast<const T*>(it->second.get());
    }

    /**
     * @brief Retrieves a reference to the typed value in the per-request data bag, throwing std::runtime_error if not set.
     */
    template <typename T>
    [[nodiscard]] T& local() {
        T* ptr = get<T>();
        if (!ptr) {
            throw std::runtime_error("Context: local value of type '" + std::string(typeid(T).name()) + "' was not found.");
        }
        return *ptr;
    }

    template <typename T>
    [[nodiscard]] const T& local() const {
        const T* ptr = get<T>();
        if (!ptr) {
            throw std::runtime_error("Context: local value of type '" + std::string(typeid(T).name()) + "' was not found.");
        }
        return *ptr;
    }

    /**
     * @brief Checks if a typed value is present in the per-request data bag.
     */
    template <typename T>
    [[nodiscard]] bool has() const noexcept {
        return local_store_.find(std::type_index(typeid(T))) != local_store_.end();
    }

    // Core accessors
    [[nodiscard]] Request& req() noexcept { return req_; }
    [[nodiscard]] const Request& req() const noexcept { return req_; }

    [[nodiscard]] Response& res() noexcept { return res_; }
    [[nodiscard]] const Response& res() const noexcept { return res_; }

    /**
     * @brief Access the ServiceRegistry pointer.
     */
    [[nodiscard]] const ServiceRegistry* services() const noexcept {
        return services_;
    }

    /**
     * @brief Retrieve a required service by type, throwing std::runtime_error if not found.
     */
    template <typename T>
    [[nodiscard]] T& service(std::string_view name = "") const {
        if (!services_) {
            throw std::runtime_error("Context: ServiceRegistry is not attached.");
        }
        return services_->require<T>(name);
    }

    /**
     * @brief Checks if a service of type T (and optional name) is registered.
     */
    template <typename T>
    [[nodiscard]] bool has_service(std::string_view name = "") const noexcept {
        return services_ ? services_->has<T>(name) : false;
    }

    /**
     * @brief Retrieve an optional service by type (and optional name), returning nullptr if not registered.
     */
    template <typename T>
    [[nodiscard]] T* try_service(std::string_view name = "") const noexcept {
        return services_ ? services_->get<T>(name) : nullptr;
    }

    /**
     * @brief Backward-compatible alias for try_service<T>().
     */
    template <typename T>
    [[nodiscard]] T* state() const noexcept {
        return try_service<T>();
    }

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

    /**
     * @brief Emits a standardized RFC 7807 Problem Details response.
     */
    Response& problem(StatusCode status, std::string_view title, std::string_view detail = "", std::string_view type = "about:blank") {
        ProblemDetails pd{
            .type = std::string(type),
            .title = std::string(title),
            .status = static_cast<int>(status),
            .detail = std::string(detail),
            .instance = std::string(req_.path())
        };
        res_.header("content-type", "application/problem+json");
        return res_.status(status).json(pd.to_json());
    }

    /**
     * @brief Serves a static file directly with zero-copy kernel streaming.
     */
    Response& send_file(const std::string& filepath, std::string_view mime_type = "") {
        return res_.file(filepath, mime_type);
    }
};

} // namespace aegon::http
