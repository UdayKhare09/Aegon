#pragma once

#include "http/Context.h"
#include "http/grpc/GrpcStatus.h"
#include "http/grpc/GrpcCodec.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <functional>
#include <utility>

namespace aegon::http::grpc {

using GrpcWriteFn = std::function<core::Task<void>(std::string)>;

/**
 * @brief Stack-allocated Context wrapper passed to gRPC Unary RPC handlers.
 *
 * Provides typed access to the deserialized request message (req()), the response builder
 * (reply()), status error responses (error()), and full transparent access to Aegon's
 * thread-per-core ServiceRegistry (service<T>()) and per-request data bag (set/get<T>()).
 */
template <typename Req, typename Res>
class UnaryContext {
private:
    Context& http_ctx_;
    const Req& req_;
    bool replied_{false};

public:
    UnaryContext(Context& http_ctx, const Req& req) noexcept
        : http_ctx_(http_ctx), req_(req) {}

    /**
     * @brief Access the deserialized Protobuf request message.
     */
    [[nodiscard]] const Req& req() const noexcept { return req_; }

    /**
     * @brief Underlying Aegon HTTP Context reference.
     */
    [[nodiscard]] Context& http() noexcept { return http_ctx_; }
    [[nodiscard]] const Context& http() const noexcept { return http_ctx_; }
    [[nodiscard]] Request& raw_req() noexcept { return http_ctx_.req(); }
    [[nodiscard]] Response& raw_res() noexcept { return http_ctx_.res(); }

    /**
     * @brief Access the shared ServiceRegistry (e.g. Database, Redis, domain state).
     */
    template <typename T>
    [[nodiscard]] T& service(std::string_view name = "") const {
        return http_ctx_.service<T>(name);
    }

    template <typename T>
    [[nodiscard]] bool has_service(std::string_view name = "") const noexcept {
        return http_ctx_.has_service<T>(name);
    }

    template <typename T>
    [[nodiscard]] T* try_service(std::string_view name = "") const noexcept {
        return http_ctx_.try_service<T>(name);
    }

    /**
     * @brief Store a per-request typed value for middlewares / interceptors.
     */
    template <typename T>
    void set(T val) {
        http_ctx_.set<T>(std::move(val));
    }

    template <typename T>
    [[nodiscard]] T* get() noexcept {
        return http_ctx_.get<T>();
    }

    template <typename T>
    [[nodiscard]] const T* get() const noexcept {
        return http_ctx_.get<T>();
    }

    template <typename T>
    [[nodiscard]] T& local() {
        return http_ctx_.local<T>();
    }

    /**
     * @brief Emits a successful gRPC response with 5-byte HTTP/2 framing and grpc-status: 0 trailer.
     */
    void reply(const Res& res) {
        std::string body;
        GrpcCodec<Res>::encode(res, body);

        http_ctx_.res().status(http::StatusCode::Ok);
        http_ctx_.res().header("content-type", "application/grpc");
        http_ctx_.res().body(std::move(body));
        http_ctx_.res().trailer("grpc-status", "0");
        replied_ = true;
    }

    /**
     * @brief Emits an RFC-compliant gRPC Trailers-Only error response.
     */
    void error(StatusCode code, std::string_view message = "") {
        http_ctx_.res().status(http::StatusCode::Ok);
        http_ctx_.res().header("content-type", "application/grpc");
        http_ctx_.res().trailers_only(true);
        http_ctx_.res().trailer("grpc-status", status_code_to_string(code));
        if (!message.empty()) {
            http_ctx_.res().set_trailer_owned("grpc-message", std::string(message));
        }
        replied_ = true;
    }

    [[nodiscard]] bool has_replied() const noexcept { return replied_; }
};

/**
 * @brief Stack-allocated Context wrapper passed to gRPC Server Streaming RPC handlers.
 *
 * Allows asynchronously pushing multiple response messages to the client via co_await ctx.send(res).
 */
template <typename Req, typename Res>
class ServerStreamingContext {
private:
    Context& http_ctx_;
    const Req& req_;
    GrpcWriteFn writer_{nullptr};
    bool error_emitted_{false};

public:
    ServerStreamingContext(Context& http_ctx, const Req& req, GrpcWriteFn writer) noexcept
        : http_ctx_(http_ctx), req_(req), writer_(std::move(writer)) {}

    [[nodiscard]] const Req& req() const noexcept { return req_; }
    [[nodiscard]] Context& http() noexcept { return http_ctx_; }
    [[nodiscard]] const Context& http() const noexcept { return http_ctx_; }
    [[nodiscard]] Request& raw_req() noexcept { return http_ctx_.req(); }
    [[nodiscard]] Response& raw_res() noexcept { return http_ctx_.res(); }

    template <typename T>
    [[nodiscard]] T& service(std::string_view name = "") const {
        return http_ctx_.service<T>(name);
    }

    template <typename T>
    [[nodiscard]] bool has_service(std::string_view name = "") const noexcept {
        return http_ctx_.has_service<T>(name);
    }

    template <typename T>
    [[nodiscard]] T* try_service(std::string_view name = "") const noexcept {
        return http_ctx_.try_service<T>(name);
    }

    template <typename T>
    void set(T val) {
        http_ctx_.set<T>(std::move(val));
    }

    template <typename T>
    [[nodiscard]] T* get() noexcept {
        return http_ctx_.get<T>();
    }

    /**
     * @brief Serializes and flushes a single 5-byte framed gRPC response message to the HTTP/2 stream.
     */
    core::Task<void> send(const Res& res) {
        if (error_emitted_) co_return;

        std::string frame;
        GrpcCodec<Res>::encode(res, frame);
        if (writer_) {
            co_await writer_(std::move(frame));
        }
    }

    /**
     * @brief Emits a gRPC error status trailer.
     */
    void error(StatusCode code, std::string_view message = "") {
        error_emitted_ = true;
        http_ctx_.res().status(http::StatusCode::Ok);
        http_ctx_.res().header("content-type", "application/grpc");
        http_ctx_.res().trailer("grpc-status", status_code_to_string(code));
        if (!message.empty()) {
            http_ctx_.res().set_trailer_owned("grpc-message", std::string(message));
        }
    }

    [[nodiscard]] bool has_error() const noexcept { return error_emitted_; }
};

} // namespace aegon::http::grpc
