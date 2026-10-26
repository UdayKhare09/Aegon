#pragma once

#include "http/Router.h"
#include "http/RouteGroup.h"
#include "http/grpc/GrpcStatus.h"
#include "http/grpc/GrpcCodec.h"
#include "http/grpc/GrpcContext.h"
#include "core/Task.h"
#include <string>
#include <string_view>
#include <vector>
#include <type_traits>
#include <concepts>

namespace aegon::http::grpc {

/**
 * @brief Scoped gRPC Service Group DSL for registering typed RPC methods and interceptors.
 *
 * Example:
 *   auto svc = router.grpc_service("/benchmark.BenchmarkService");
 *   svc.unary<SumRequest, SumReply>("GetSum", [](auto& ctx) { ... });
 */
class GrpcServiceGroup {
private:
    Router& router_;
    std::string prefix_;
    std::vector<MiddlewareFn> interceptors_;

public:
    GrpcServiceGroup(Router& router, std::string prefix, std::vector<MiddlewareFn> interceptors = {})
        : router_(router), prefix_(std::move(prefix)), interceptors_(std::move(interceptors)) {}

    /**
     * @brief Appends an interceptor middleware to this gRPC service group.
     */
    template <typename F>
    GrpcServiceGroup& use(F&& interceptor) {
        interceptors_.push_back(make_middleware(std::forward<F>(interceptor)));
        return *this;
    }

    /**
     * @brief Registers a synchronous Unary RPC endpoint (zero-allocation hot path).
     *
     * Handlers returning void execute synchronously with 0 coroutines and 0 task allocations.
     */
    template <typename Req, typename Res, typename F>
        requires (std::is_invocable_v<F, UnaryContext<Req, Res>&> &&
                 !std::is_invocable_r_v<core::Task<void>, F, UnaryContext<Req, Res>&>)
    GrpcServiceGroup& unary(std::string_view method_name, F&& handler) {
        std::string path = join_paths(prefix_, method_name);

        router_.add_route(Method::POST, path, interceptors_, {},
            [fn = std::forward<F>(handler)](Context& ctx) {
                if (!ctx.req().is_grpc()) [[unlikely]] {
                    ctx.res().status(http::StatusCode::Ok);
                    ctx.res().header("content-type", "application/grpc");
                    ctx.res().trailers_only(true);
                    ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::InvalidArgument));
                    ctx.res().set_trailer_owned("grpc-message", "Content-Type must be application/grpc");
                    return;
                }

                Req req_msg;
                if (!GrpcCodec<Req>::decode(ctx.req().body(), req_msg)) [[unlikely]] {
                    ctx.res().status(http::StatusCode::Ok);
                    ctx.res().header("content-type", "application/grpc");
                    ctx.res().trailers_only(true);
                    ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::InvalidArgument));
                    ctx.res().set_trailer_owned("grpc-message", "Invalid or malformed gRPC frame payload");
                    return;
                }

                UnaryContext<Req, Res> uctx(ctx, req_msg);
                fn(uctx);

                if (!uctx.has_replied()) [[unlikely]] {
                    if (!ctx.res().trailers().contains("grpc-status")) {
                        ctx.res().status(http::StatusCode::Ok);
                        ctx.res().header("content-type", "application/grpc");
                        ctx.res().trailers_only(true);
                        ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::Internal));
                        ctx.res().set_trailer_owned("grpc-message", "Handler completed without sending reply");
                    }
                }
            }
        );
        return *this;
    }

    /**
     * @brief Registers an asynchronous coroutine Unary RPC endpoint (C++23 Task<void>).
     */
    template <typename Req, typename Res, typename F>
        requires std::is_invocable_r_v<core::Task<void>, F, UnaryContext<Req, Res>&>
    GrpcServiceGroup& unary(std::string_view method_name, F&& handler) {
        std::string path = join_paths(prefix_, method_name);

        router_.add_route(Method::POST, path, interceptors_, {},
            [fn = std::forward<F>(handler)](Context& ctx) -> core::Task<void> {
                if (!ctx.req().is_grpc()) [[unlikely]] {
                    ctx.res().status(http::StatusCode::Ok);
                    ctx.res().header("content-type", "application/grpc");
                    ctx.res().trailers_only(true);
                    ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::InvalidArgument));
                    ctx.res().set_trailer_owned("grpc-message", "Content-Type must be application/grpc");
                    co_return;
                }

                Req req_msg;
                if (!GrpcCodec<Req>::decode(ctx.req().body(), req_msg)) [[unlikely]] {
                    ctx.res().status(http::StatusCode::Ok);
                    ctx.res().header("content-type", "application/grpc");
                    ctx.res().trailers_only(true);
                    ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::InvalidArgument));
                    ctx.res().set_trailer_owned("grpc-message", "Invalid or malformed gRPC frame payload");
                    co_return;
                }

                UnaryContext<Req, Res> uctx(ctx, req_msg);
                co_await fn(uctx);

                if (!uctx.has_replied()) [[unlikely]] {
                    if (!ctx.res().trailers().contains("grpc-status")) {
                        ctx.res().status(http::StatusCode::Ok);
                        ctx.res().header("content-type", "application/grpc");
                        ctx.res().trailers_only(true);
                        ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::Internal));
                        ctx.res().set_trailer_owned("grpc-message", "Handler completed without sending reply");
                    }
                }
            }
        );
        return *this;
    }

    /**
     * @brief Registers a Server Streaming RPC endpoint (1 request -> stream of responses).
     */
    template <typename Req, typename Res, typename F>
        requires std::is_invocable_r_v<core::Task<void>, F, ServerStreamingContext<Req, Res>&>
    GrpcServiceGroup& server_streaming(std::string_view method_name, F&& handler) {
        std::string path = join_paths(prefix_, method_name);

        router_.add_route(Method::POST, path, interceptors_, {},
            [fn = std::forward<F>(handler)](Context& ctx) -> core::Task<void> {
                if (!ctx.req().is_grpc()) [[unlikely]] {
                    ctx.res().status(http::StatusCode::Ok);
                    ctx.res().header("content-type", "application/grpc");
                    ctx.res().trailers_only(true);
                    ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::InvalidArgument));
                    ctx.res().set_trailer_owned("grpc-message", "Content-Type must be application/grpc");
                    co_return;
                }

                Req req_msg;
                if (!GrpcCodec<Req>::decode(ctx.req().body(), req_msg)) [[unlikely]] {
                    ctx.res().status(http::StatusCode::Ok);
                    ctx.res().header("content-type", "application/grpc");
                    ctx.res().trailers_only(true);
                    ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::InvalidArgument));
                    ctx.res().set_trailer_owned("grpc-message", "Invalid or malformed gRPC frame payload");
                    co_return;
                }

                ctx.res().status(http::StatusCode::Ok);
                ctx.res().header("content-type", "application/grpc");

                ServerStreamingContext<Req, Res> sctx(ctx, req_msg, ctx.grpc_writer());
                co_await fn(sctx);

                if (!sctx.has_error()) {
                    if (!ctx.res().trailers().contains("grpc-status")) {
                        ctx.res().trailer("grpc-status", "0");
                    }
                }
            }
        );
        return *this;
    }

    /**
     * @brief Registers a Client Streaming RPC endpoint (stream of requests -> 1 response).
     */
    template <typename Req, typename Res, typename F>
        requires std::is_invocable_r_v<core::Task<void>, F, ClientStreamingContext<Req, Res>&>
    GrpcServiceGroup& client_streaming(std::string_view method_name, F&& handler) {
        std::string path = join_paths(prefix_, method_name);

        router_.add_route(Method::POST, path, interceptors_, {},
            [fn = std::forward<F>(handler)](Context& ctx) -> core::Task<void> {
                if (!ctx.req().is_grpc()) [[unlikely]] {
                    ctx.res().status(http::StatusCode::Ok);
                    ctx.res().header("content-type", "application/grpc");
                    ctx.res().trailers_only(true);
                    ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::InvalidArgument));
                    ctx.res().set_trailer_owned("grpc-message", "Content-Type must be application/grpc");
                    co_return;
                }

                ctx.res().status(http::StatusCode::Ok);
                ctx.res().header("content-type", "application/grpc");

                ClientStreamingContext<Req, Res> cctx(ctx, ctx.grpc_inbound(), ctx.grpc_writer());
                co_await fn(cctx);

                if (!cctx.has_replied() && !cctx.has_error()) [[unlikely]] {
                    if (!ctx.res().trailers().contains("grpc-status")) {
                        ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::Internal));
                        ctx.res().set_trailer_owned("grpc-message", "Handler completed without sending reply");
                    }
                } else if (cctx.has_replied() && !cctx.has_error()) {
                    if (!ctx.res().trailers().contains("grpc-status")) {
                        ctx.res().trailer("grpc-status", "0");
                    }
                }
            },
            true // is_streaming = true
        );
        return *this;
    }

    /**
     * @brief Registers a Bidirectional Streaming RPC endpoint (stream of requests <-> stream of responses).
     */
    template <typename Req, typename Res, typename F>
        requires std::is_invocable_r_v<core::Task<void>, F, BidiStreamingContext<Req, Res>&>
    GrpcServiceGroup& bidi_streaming(std::string_view method_name, F&& handler) {
        std::string path = join_paths(prefix_, method_name);

        router_.add_route(Method::POST, path, interceptors_, {},
            [fn = std::forward<F>(handler)](Context& ctx) -> core::Task<void> {
                if (!ctx.req().is_grpc()) [[unlikely]] {
                    ctx.res().status(http::StatusCode::Ok);
                    ctx.res().header("content-type", "application/grpc");
                    ctx.res().trailers_only(true);
                    ctx.res().trailer("grpc-status", status_code_to_string(StatusCode::InvalidArgument));
                    ctx.res().set_trailer_owned("grpc-message", "Content-Type must be application/grpc");
                    co_return;
                }

                ctx.res().status(http::StatusCode::Ok);
                ctx.res().header("content-type", "application/grpc");

                BidiStreamingContext<Req, Res> bctx(ctx, ctx.grpc_inbound(), ctx.grpc_writer());
                co_await fn(bctx);

                if (!bctx.has_error()) {
                    if (!ctx.res().trailers().contains("grpc-status")) {
                        ctx.res().trailer("grpc-status", "0");
                    }
                }
            },
            true // is_streaming = true
        );
        return *this;
    }
};

} // namespace aegon::http::grpc
