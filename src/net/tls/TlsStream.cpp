#include "net/tls/TlsStream.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#if defined(__linux__)
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <linux/tls.h>
#endif
#include <cstring>
#include <iostream>
#include <cstdlib>

namespace aegon::net::tls {

int get_tls_stream_ex_index() noexcept {
    static int idx = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return idx;
}

TlsStream::TlsStream(core::EventLoop& loop, core::os_socket_t client_fd, SSL_CTX* ctx)
    : loop_(&loop), client_fd_(client_fd) {
    ssl_ = SSL_new(ctx);
    in_bio_ = BIO_new(BIO_s_mem());
    out_bio_ = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl_, in_bio_, out_bio_);
    SSL_set_accept_state(ssl_);
    SSL_set_ex_data(ssl_, get_tls_stream_ex_index(), this);
}

TlsStream::~TlsStream() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
}

TlsStream::TlsStream(TlsStream&& other) noexcept
    : loop_(other.loop_), client_fd_(other.client_fd_),
      ssl_(other.ssl_), in_bio_(other.in_bio_), out_bio_(other.out_bio_),
      handshake_done_(other.handshake_done_), ktls_tx_(other.ktls_tx_),
      server_traffic_secret_(std::move(other.server_traffic_secret_)) {
    other.ssl_ = nullptr;
    other.client_fd_ = core::INVALID_OS_SOCKET;
    if (ssl_) {
        SSL_set_ex_data(ssl_, get_tls_stream_ex_index(), this);
    }
}

TlsStream& TlsStream::operator=(TlsStream&& other) noexcept {
    if (this != &other) {
        if (ssl_) SSL_free(ssl_);
        loop_ = other.loop_;
        client_fd_ = other.client_fd_;
        ssl_ = other.ssl_;
        in_bio_ = other.in_bio_;
        out_bio_ = other.out_bio_;
        handshake_done_ = other.handshake_done_;
        ktls_tx_ = other.ktls_tx_;
        server_traffic_secret_ = std::move(other.server_traffic_secret_);

        other.ssl_ = nullptr;
        other.client_fd_ = core::INVALID_OS_SOCKET;
        if (ssl_) {
            SSL_set_ex_data(ssl_, get_tls_stream_ex_index(), this);
        }
    }
    return *this;
}

core::Task<bool> TlsStream::flush_outbound() {
    char out_buf[65536];
    while (BIO_pending(out_bio_) > 0) {
        int n = BIO_read(out_bio_, out_buf, sizeof(out_buf));
        if (n > 0) {
            int sent = co_await loop_->engine().send_all(
                client_fd_,
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(out_buf), static_cast<size_t>(n)));
            if (sent != n) {
                co_return false;
            }
        }
    }
    co_return true;
}

bool TlsStream::try_enable_ktls_tx() {
    if (server_traffic_secret_.empty() || client_fd_ < 0) return false;

    const char* env_disable = std::getenv("AEGON_DISABLE_KTLS");
    if (env_disable && (std::strcmp(env_disable, "1") == 0 || std::strcmp(env_disable, "true") == 0)) {
        return false;
    }

#if defined(__linux__)
    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
    if (!cipher) return false;
    uint32_t cipher_id = SSL_CIPHER_get_id(cipher);

    const EVP_MD* md = nullptr;
    size_t secret_len = 0;
    size_t key_len = 0;
    uint16_t cipher_type = 0;

    if (cipher_id == 0x03001301) { // TLS_AES_128_GCM_SHA256
        md = EVP_sha256();
        secret_len = 32;
        key_len = 16;
        cipher_type = 51; // TLS_CIPHER_AES_GCM_128
    } else if (cipher_id == 0x03001302) { // TLS_AES_256_GCM_SHA384
        md = EVP_sha384();
        secret_len = 48;
        key_len = 32;
        cipher_type = 52; // TLS_CIPHER_AES_GCM_256
    } else {
        return false;
    }

    if (server_traffic_secret_.size() < secret_len * 2) return false;
    uint8_t secret[64];
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (size_t i = 0; i < secret_len; ++i) {
        secret[i] = static_cast<uint8_t>((hex_val(server_traffic_secret_[2 * i]) << 4) |
                                          hex_val(server_traffic_secret_[2 * i + 1]));
    }

    auto hkdf_expand_label = [md](const uint8_t* sec, size_t sec_len,
                                  const char* label, size_t label_len,
                                  uint16_t out_len, uint8_t* out) {
        uint8_t info[64];
        info[0] = static_cast<uint8_t>((out_len >> 8) & 0xFF);
        info[1] = static_cast<uint8_t>(out_len & 0xFF);
        const char* prefix = "tls13 ";
        size_t prefix_len = 6;
        info[2] = static_cast<uint8_t>(prefix_len + label_len);
        std::memcpy(info + 3, prefix, prefix_len);
        std::memcpy(info + 3 + prefix_len, label, label_len);
        info[3 + prefix_len + label_len] = 0; // context len = 0
        size_t info_len = 4 + prefix_len + label_len;

        unsigned int hlen = 0;
        uint8_t step1[EVP_MAX_MD_SIZE];
        HMAC(md, sec, static_cast<int>(sec_len), info, info_len, step1, &hlen);
        uint8_t input2[EVP_MAX_MD_SIZE + 64];
        std::memcpy(input2, step1, hlen);
        std::memcpy(input2 + hlen, info, info_len);
        input2[hlen + info_len] = 1;
        uint8_t step2[EVP_MAX_MD_SIZE];
        HMAC(md, sec, static_cast<int>(sec_len), input2, hlen + info_len + 1, step2, &hlen);
        std::memcpy(out, step2, out_len);
    };

    uint8_t write_key[32];
    uint8_t write_iv[12];
    hkdf_expand_label(secret, secret_len, "key", 3, static_cast<uint16_t>(key_len), write_key);
    hkdf_expand_label(secret, secret_len, "iv", 2, 12, write_iv);

    if (setsockopt(client_fd_, SOL_TCP, TCP_ULP, "tls", sizeof("tls")) != 0) {
        return false;
    }

    if (cipher_type == 51) {
        tls12_crypto_info_aes_gcm_128 crypto_info{};
        crypto_info.info.version = TLS_1_3_VERSION;
        crypto_info.info.cipher_type = cipher_type;
        std::memcpy(crypto_info.key, write_key, 16);
        std::memcpy(crypto_info.iv, write_iv, 4);
        std::memcpy(crypto_info.salt, write_iv + 4, 8);
        std::memset(crypto_info.rec_seq, 0, sizeof(crypto_info.rec_seq));
        if (setsockopt(client_fd_, SOL_TLS, TLS_TX, &crypto_info, sizeof(crypto_info)) != 0) {
            return false;
        }
    } else {
        tls12_crypto_info_aes_gcm_256 crypto_info{};
        crypto_info.info.version = TLS_1_3_VERSION;
        crypto_info.info.cipher_type = cipher_type;
        std::memcpy(crypto_info.key, write_key, 32);
        std::memcpy(crypto_info.iv, write_iv, 4);
        std::memcpy(crypto_info.salt, write_iv + 4, 8);
        std::memset(crypto_info.rec_seq, 0, sizeof(crypto_info.rec_seq));
        if (setsockopt(client_fd_, SOL_TLS, TLS_TX, &crypto_info, sizeof(crypto_info)) != 0) {
            return false;
        }
    }

    ktls_tx_ = true;
    return true;
#else
    return false;
#endif
}

core::Task<bool> TlsStream::handshake() {
    char net_in[16384];

    while (!SSL_is_init_finished(ssl_)) {
        int ret = SSL_do_handshake(ssl_);

        if (!co_await flush_outbound()) {
            co_return false;
        }

        if (ret == 1) {
            handshake_done_ = true;
            try_enable_ktls_tx();
            co_return true;
        }

        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ) {
            int n = co_await loop_->engine().recv(client_fd_, net_in, sizeof(net_in));
            if (n <= 0) {
                co_return false;
            }
            BIO_write(in_bio_, net_in, n);
        } else if (err == SSL_ERROR_WANT_WRITE) {
            if (!co_await flush_outbound()) {
                co_return false;
            }
        } else {
            co_return false;
        }
    }

    handshake_done_ = true;
    try_enable_ktls_tx();
    co_return true;
}

core::Task<int> TlsStream::read_plaintext(void* buf, size_t max_len) {
    char net_in[16384];

    while (true) {
        int ret = SSL_read(ssl_, buf, static_cast<int>(max_len));
        if (ret > 0) {
            co_return ret;
        }

        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_ZERO_RETURN) {
            co_return 0; // Clean TLS shutdown
        }

        if (err == SSL_ERROR_WANT_READ) {
            int n = co_await loop_->engine().recv(client_fd_, net_in, sizeof(net_in));
            if (n <= 0) {
                co_return n;
            }
            BIO_write(in_bio_, net_in, n);
        } else if (err == SSL_ERROR_WANT_WRITE) {
            if (!co_await flush_outbound()) {
                co_return -1;
            }
        } else {
            co_return -1;
        }
    }
}

core::Task<int> TlsStream::write_plaintext(const void* buf, size_t len) {
    if (len == 0) co_return 0;

    if (ktls_tx_) {
        // kTLS TX offload: bypass OpenSSL encryption completely!
        co_return co_await loop_->engine().send_all(
            client_fd_,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(buf), len));
    }

    int written = SSL_write(ssl_, buf, static_cast<int>(len));
    if (written <= 0) {
        co_return written;
    }

    if (!co_await flush_outbound()) {
        co_return -1;
    }

    co_return written;
}

std::string_view TlsStream::alpn() const noexcept {
    if (!ssl_) return {};
    const unsigned char* proto = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(ssl_, &proto, &len);
    if (!proto || len == 0) return {};
    return std::string_view(reinterpret_cast<const char*>(proto), len);
}

} // namespace aegon::net::tls

