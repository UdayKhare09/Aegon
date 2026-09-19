#pragma once

#include "core/EventLoop.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <string>
#include <string_view>
#include <optional>
#include <memory>

namespace aegon::http::client {

struct TlsClientOptions {
    bool verify_peer{true};
    bool verify_hostname{true};
    bool insecure_skip_verify{false};
    std::optional<std::string> ca_bundle_path{};
    std::optional<std::string> client_cert_path{};
    std::optional<std::string> client_key_path{};
};

class TlsClientContext {
public:
    explicit TlsClientContext(const TlsClientOptions& options = {});
    ~TlsClientContext();

    TlsClientContext(const TlsClientContext&) = delete;
    TlsClientContext& operator=(const TlsClientContext&) = delete;
    TlsClientContext(TlsClientContext&& other) noexcept;
    TlsClientContext& operator=(TlsClientContext&& other) noexcept;

    [[nodiscard]] SSL_CTX* native_handle() noexcept { return ctx_; }
    [[nodiscard]] bool is_valid() const noexcept { return ctx_ != nullptr; }
    [[nodiscard]] const TlsClientOptions& options() const noexcept { return options_; }

private:
    SSL_CTX* ctx_{nullptr};
    TlsClientOptions options_{};
};

class TlsClientStream {
public:
    TlsClientStream(core::EventLoop& loop, int socket_fd, SSL_CTX* ctx, 
                    std::string_view hostname, const TlsClientOptions& options = {});
    ~TlsClientStream();

    TlsClientStream(const TlsClientStream&) = delete;
    TlsClientStream& operator=(const TlsClientStream&) = delete;

    /**
     * @brief Performs the async TLS client handshake over io_uring.
     * @return true on handshake success, false on verification failure or socket close.
     */
    core::Task<bool> handshake();

    /**
     * @brief Reads decrypted application plaintext from the TLS connection.
     */
    core::Task<int> read_plaintext(void* buf, size_t max_len);

    /**
     * @brief Encrypts and transmits application plaintext over io_uring.
     */
    core::Task<int> write_plaintext(const void* buf, size_t len);

    /**
     * @brief Returns negotiated ALPN protocol ("h2", "http/1.1", or empty).
     */
    [[nodiscard]] std::string_view alpn() const noexcept;

    [[nodiscard]] bool is_h2() const noexcept { return alpn() == "h2"; }

    void set_loop(core::EventLoop& loop) noexcept { loop_ = &loop; }

private:
    core::Task<bool> flush_outbound();

    core::EventLoop* loop_{nullptr};
    int socket_fd_{-1};
    SSL* ssl_{nullptr};
    BIO* in_bio_{nullptr};
    BIO* out_bio_{nullptr};
    bool handshake_done_{false};
    std::string hostname_{};
};

} // namespace aegon::http::client
