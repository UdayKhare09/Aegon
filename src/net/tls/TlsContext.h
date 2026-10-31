#pragma once

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace aegon::net::tls {

/**
 * @brief Manages OpenSSL SSL_CTX configuration, TLS 1.3/1.2 parameters,
 * ALPN protocol selection, certificate lifecycle, and in-memory self-signed generation.
 */
class TlsContext {
public:
    TlsContext();
    ~TlsContext();

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    TlsContext(TlsContext&& other) noexcept;
    TlsContext& operator=(TlsContext&& other) noexcept;

    /**
     * @brief Load PEM certificate and private key files.
     */
    bool load_cert_and_key(const std::string& cert_path, const std::string& key_path);

    /**
     * @brief Generate in-memory self-signed certificate (RSA 2048) for local dev & testing.
     */
    bool generate_self_signed(const std::string& common_name = "localhost");

    /**
     * @brief Configures ALPN advertisement list (e.g. {"h2", "http/1.1"} or custom protocols).
     */
    void set_alpn_protocols(const std::vector<std::string>& protocols);

    [[nodiscard]] SSL_CTX* native_handle() noexcept { return ctx_; }
    [[nodiscard]] bool is_valid() const noexcept { return ctx_ != nullptr; }
    [[nodiscard]] const std::vector<unsigned char>& alpn_wire() const noexcept { return alpn_wire_; }

private:
    void setup_alpn();

    SSL_CTX* ctx_{nullptr};
    std::vector<unsigned char> alpn_wire_{};
};

} // namespace aegon::net::tls
