#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "http/ServiceRegistry.h"
#include "http/ProblemDetails.h"
#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>

namespace aegon::http {

/**
 * @brief Zero-overhead compile-time Context passed to route handlers.
 *
 * Provides direct access to inbound request data (req), outbound response builder (res),
 * DTO binders (bind_json, bind_query, bind_path), and the type-safe ServiceRegistry.
 */
class Context {
private:
    Request& req_;
    Response& res_;
    const ServiceRegistry* services_{nullptr};

public:
    Context(Request& req, Response& res, const ServiceRegistry* services = nullptr) noexcept
        : req_(req), res_(res), services_(services) {}

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
    [[nodiscard]] T& service() const {
        if (!services_) {
            throw std::runtime_error("Context: ServiceRegistry is not attached.");
        }
        return services_->require<T>();
    }

    /**
     * @brief Checks if a service of type T is registered.
     */
    template <typename T>
    [[nodiscard]] bool has_service() const noexcept {
        return services_ ? services_->has<T>() : false;
    }

    /**
     * @brief Retrieve an optional service by type, returning nullptr if not registered.
     */
    template <typename T>
    [[nodiscard]] T* try_service() const noexcept {
        return services_ ? services_->get<T>() : nullptr;
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
