#pragma once

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <string>
#include <string_view>
#include <memory>

namespace aegon::http::tls {

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
     * @brief Generate in-memory self-signed certificate (RSA 2048) for local dev & automated testing.
     */
    bool generate_self_signed(const std::string& common_name = "localhost");

    [[nodiscard]] SSL_CTX* native_handle() noexcept { return ctx_; }
    [[nodiscard]] bool is_valid() const noexcept { return ctx_ != nullptr; }

private:
    void setup_alpn();

    SSL_CTX* ctx_{nullptr};
};

} // namespace aegon::http::tls
