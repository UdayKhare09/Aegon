#pragma once

#include "http/Request.h"
#include "http/Response.h"

namespace aegon::http {

/**
 * @brief Zero-overhead compile-time Context passed to all fluent route handlers.
 *
 * Provides direct access to request data, response builder, route parameters,
 * SIMD UUID extraction, and typed user state / database connection pool.
 */
class Context {
public:
    Context(Request& req, Response& res, void* user_state = nullptr) noexcept
        : req_(req), res_(res), user_state_(user_state) {}

    // Core accessors
    [[nodiscard]] Request& req() noexcept { return req_; }
    [[nodiscard]] const Request& req() const noexcept { return req_; }

    [[nodiscard]] Response& res() noexcept { return res_; }
    [[nodiscard]] const Response& res() const noexcept { return res_; }

    // Fluent helpers mapping to Request
    [[nodiscard]] std::string_view path() const noexcept { return req_.path(); }
    [[nodiscard]] Method method() const noexcept { return req_.method(); }
    [[nodiscard]] std::string_view body() const noexcept { return req_.body(); }

    [[nodiscard]] std::optional<std::string_view> param(std::string_view key) const noexcept {
        return req_.param(key);
    }

    [[nodiscard]] std::optional<aegon::data::UUID> param_uuid(std::string_view key) const noexcept {
        return req_.param_uuid(key);
    }

    [[nodiscard]] std::optional<std::string_view> query(std::string_view key) const noexcept {
        return req_.query_param(key);
    }

    [[nodiscard]] std::optional<std::string_view> header(std::string_view key) const noexcept {
        return req_.headers().get(key);
    }

    // Fluent helpers mapping to Response
    Context& status(StatusCode code) noexcept {
        res_.status(code);
        return *this;
    }

    Context& status(uint16_t code) noexcept {
        res_.status(code);
        return *this;
    }

    Context& header(std::string_view name, std::string_view value) {
        res_.header(name, value);
        return *this;
    }

    Context& text(std::string_view t) {
        res_.text(t);
        return *this;
    }

    Context& json(std::string_view j) {
        res_.json(j);
        return *this;
    }

    Context& html(std::string_view h) {
        res_.html(h);
        return *this;
    }

    Context& uuid(const aegon::data::UUID& id) {
        res_.uuid(id);
        return *this;
    }

    // Typed user state / database connection pool accessor (zero-cost DI)
    template <typename T>
    [[nodiscard]] T* state() noexcept {
        return static_cast<T*>(user_state_);
    }

    template <typename T>
    [[nodiscard]] const T* state() const noexcept {
        return static_cast<const T*>(user_state_);
    }

private:
    Request& req_;
    Response& res_;
    void* user_state_{nullptr};
};

} // namespace aegon::http
