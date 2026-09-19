#include "http/client/TlsClientStream.h"
#include <iostream>

namespace aegon::http::client {

namespace {

static const unsigned char CLIENT_ALPN[] = "\x08http/1.1";
static const unsigned int CLIENT_ALPN_LEN = sizeof(CLIENT_ALPN) - 1;

} // anonymous namespace

TlsClientContext::TlsClientContext(const TlsClientOptions& options)
    : options_(options) {
    const SSL_METHOD* method = TLS_client_method();
    ctx_ = SSL_CTX_new(method);
    if (!ctx_) return;

    // Enforce modern TLS 1.2 minimum
    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

    // Advertise ALPN protocols (h2, http/1.1)
    SSL_CTX_set_alpn_protos(ctx_, CLIENT_ALPN, CLIENT_ALPN_LEN);

    if (options_.insecure_skip_verify) {
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
        // Load default system CA certificates
        SSL_CTX_set_default_verify_paths(ctx_);

        if (options_.ca_bundle_path.has_value() && !options_.ca_bundle_path->empty()) {
            SSL_CTX_load_verify_locations(ctx_, options_.ca_bundle_path->c_str(), nullptr);
        }
    }

    // Mutual TLS (mTLS) client certificates
    if (options_.client_cert_path.has_value() && options_.client_key_path.has_value()) {
        SSL_CTX_use_certificate_chain_file(ctx_, options_.client_cert_path->c_str());
        SSL_CTX_use_PrivateKey_file(ctx_, options_.client_key_path->c_str(), SSL_FILETYPE_PEM);
    }
}

TlsClientContext::~TlsClientContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

TlsClientContext::TlsClientContext(TlsClientContext&& other) noexcept
    : ctx_(other.ctx_), options_(std::move(other.options_)) {
    other.ctx_ = nullptr;
}

TlsClientContext& TlsClientContext::operator=(TlsClientContext&& other) noexcept {
    if (this != &other) {
        if (ctx_) SSL_CTX_free(ctx_);
        ctx_ = other.ctx_;
        options_ = std::move(other.options_);
        other.ctx_ = nullptr;
    }
    return *this;
}

// -----------------------------------------------------------------------------
// TlsClientStream Implementation
// -----------------------------------------------------------------------------

TlsClientStream::TlsClientStream(core::EventLoop& loop, int socket_fd, SSL_CTX* ctx,
                                 std::string_view hostname, const TlsClientOptions& options)
    : loop_(&loop), socket_fd_(socket_fd), hostname_(hostname) {
    ssl_ = SSL_new(ctx);
    in_bio_ = BIO_new(BIO_s_mem());
    out_bio_ = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl_, in_bio_, out_bio_);
    SSL_set_connect_state(ssl_);

    // Set Server Name Indication (SNI)
    if (!hostname_.empty()) {
        SSL_set_tlsext_host_name(ssl_, hostname_.c_str());
    }

    if (options.insecure_skip_verify) {
        SSL_set_verify(ssl_, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_set_verify(ssl_, SSL_VERIFY_PEER, nullptr);
        if (options.verify_hostname && !hostname_.empty()) {
            X509_VERIFY_PARAM* param = SSL_get0_param(ssl_);
            X509_VERIFY_PARAM_set1_host(param, hostname_.data(), hostname_.size());
        }
    }
}

TlsClientStream::~TlsClientStream() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
}

core::Task<bool> TlsClientStream::flush_outbound() {
    char out_buf[4096];
    while (BIO_pending(out_bio_) > 0) {
        int n = BIO_read(out_bio_, out_buf, sizeof(out_buf));
        if (n > 0) {
            int sent = co_await loop_->ring().send(
                socket_fd_,
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(out_buf), static_cast<size_t>(n)));
            if (sent <= 0) {
                co_return false;
            }
        }
    }
    co_return true;
}

core::Task<bool> TlsClientStream::handshake() {
    while (!SSL_is_init_finished(ssl_)) {
        int ret = SSL_connect(ssl_);

        bool flushed = co_await flush_outbound();
        if (!flushed) co_return false;

        if (ret == 1) {
            handshake_done_ = true;
            co_return true;
        }

        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ) {
            char net_buf[4096];
            int n = co_await loop_->ring().recv(socket_fd_, net_buf, sizeof(net_buf), 0);
            if (n <= 0) {
                co_return false;
            }
            BIO_write(in_bio_, net_buf, n);
        } else if (err == SSL_ERROR_WANT_WRITE) {
            // Already flushed
        } else {
            co_return false;
        }
    }

    handshake_done_ = true;
    co_return true;
}

core::Task<int> TlsClientStream::read_plaintext(void* buf, size_t max_len) {
    while (true) {
        int ret = SSL_read(ssl_, buf, static_cast<int>(max_len));
        if (ret > 0) {
            co_return ret;
        }

        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_ZERO_RETURN) {
            co_return 0; // Clean shutdown
        }

        if (err == SSL_ERROR_WANT_READ) {
            char net_buf[4096];
            int n = co_await loop_->ring().recv(socket_fd_, net_buf, sizeof(net_buf), 0);
            if (n <= 0) {
                co_return 0; // EOF
            }
            BIO_write(in_bio_, net_buf, n);
        } else {
            co_return -1;
        }
    }
}

core::Task<int> TlsClientStream::write_plaintext(const void* buf, size_t len) {
    int ret = SSL_write(ssl_, buf, static_cast<int>(len));
    if (ret <= 0) {
        co_return -1;
    }

    bool flushed = co_await flush_outbound();
    if (!flushed) {
        co_return -1;
    }
    co_return ret;
}

std::string_view TlsClientStream::alpn() const noexcept {
    if (!ssl_ || !handshake_done_) return "";
    const unsigned char* proto = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(ssl_, &proto, &len);
    if (!proto || len == 0) return "";
    return std::string_view(reinterpret_cast<const char*>(proto), len);
}

} // namespace aegon::http::client
