#pragma once

#include "http/Request.h"
#include "http/Response.h"
#include "http/Router.h"
#include "http/ServiceRegistry.h"
#include "http/v1/Http1Parser.h"
#include "http/v1/Http1Serializer.h"
#include "http/tls/TlsStream.h"
#include "core/EventLoop.h"
#include "core/Task.h"
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "http/ServerConfig.h"

namespace aegon::http::v1 {

using H2DirectCallback = std::function<core::Task<void>(int client_fd, std::string preface_data)>;
using H2UpgradeCallback = std::function<core::Task<void>(int client_fd, Request req, std::string settings, std::string trailing)>;

/**
 * @brief Manages the full lifecycle of an HTTP/1.x connection (both plaintext and TLS).
 * Handles pipelining, request parsing, response batching, keep-alive,
 * RFC 9113 H2C cleartext upgrade, RFC 6455 WebSocket upgrade, and zero-copy file streaming.
 */
class Http1Connection {
public:
    Http1Connection(core::EventLoop& loop, int client_fd, const Router& router,
                    const ServiceRegistry* services = nullptr,
                    const ServerConfig& config = {});
    ~Http1Connection() = default;

    Http1Connection(const Http1Connection&) = delete;
    Http1Connection& operator=(const Http1Connection&) = delete;

    /**
     * @brief Run connection loop for plaintext HTTP/1.1 on client_fd.
     */
    core::Task<void> run(H2DirectCallback on_h2_direct = nullptr,
                         H2UpgradeCallback on_h2_upgrade = nullptr);

    /**
     * @brief Run connection loop for TLS HTTP/1.1 on tls_stream.
     */
    core::Task<void> run_tls(tls::TlsStream& tls_stream, std::string_view alt_svc_hdr = {});

    /**
     * @brief Generates standard RFC 9112 error response for parser errors.
     */
    static std::optional<Response> make_parse_error_response(ParseStatus status);

    /**
     * @brief Evaluates HTTP/1.x Connection: keep-alive / close semantics (RFC 9112 §9.3).
     */
    static bool evaluate_keep_alive(const Request& req, Response& res);

    /**
     * @brief Formats Alt-Svc advertisement header for progressive upgrade ladder.
     */
    static std::string build_alt_svc_header(uint16_t port, bool http3_enabled);

    /**
     * @brief Validates RFC 6455 Sec-WebSocket-Version header.
     */
    static std::optional<Response> validate_websocket_upgrade(const Request& req);

    /**
     * @brief Zero-copy file streaming via kernel splice pipes.
     */
    static core::Task<bool> stream_file_zero_copy(core::EventLoop& loop, int client_fd,
                                                  const std::string& file_path, size_t file_size);

private:
    core::EventLoop& loop_;
    int client_fd_;
    const Router& router_;
    const ServiceRegistry* services_;
    ServerConfig config_{};
};

} // namespace aegon::http::v1
