#include "http/tls/TlsStream.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#if defined(__linux__)
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <linux/tls.h>
#endif
#include <cstring>
#include <iostream>

namespace aegon::http::tls {

int get_tls_stream_ex_index() noexcept {
    static int idx = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return idx;
}

TlsStream::TlsStream(core::EventLoop& loop, core::os_socket_t client_fd, SSL_CTX* ctx)
    : loop_(loop), client_fd_(client_fd) {
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

core::Task<bool> TlsStream::flush_outbound() {
    char out_buf[65536];
    while (BIO_pending(out_bio_) > 0) {
        int n = BIO_read(out_bio_, out_buf, sizeof(out_buf));
        if (n > 0) {
            int sent = co_await loop_.engine().send_all(
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
        info[2] = static_cast<uint8_t>(6 + label_len);
        std::memcpy(info + 3, "tls13 ", 6);
        std::memcpy(info + 9, label, label_len);
        info[9 + label_len] = 0x00;
        info[10 + label_len] = 0x01;
        size_t info_len = 11 + label_len;

        uint8_t hmac_out[EVP_MAX_MD_SIZE];
        unsigned int hmac_len = 0;
        HMAC(md, sec, static_cast<int>(sec_len),
             info, info_len, hmac_out, &hmac_len);
        std::memcpy(out, hmac_out, out_len);
    };

    uint8_t key[32];
    uint8_t iv[12];
    hkdf_expand_label(secret, secret_len, "key", 3, static_cast<uint16_t>(key_len), key);
    hkdf_expand_label(secret, secret_len, "iv", 2, 12, iv);

    // 1. Install TCP_ULP "tls" on socket
    int ulp_ret = ::setsockopt(client_fd_, 6 /* SOL_TCP */, 31 /* TCP_ULP */, "tls", sizeof("tls") - 1);
    if (ulp_ret == 0) {
        // 2. Program TLS_TX crypto info into kernel
        if (cipher_id == 0x03001301) {
            struct tls12_crypto_info_aes_gcm_128 crypto_info{};
            crypto_info.info.version = 0x0304; // TLS_1_3_VERSION
            crypto_info.info.cipher_type = cipher_type;
            std::memcpy(crypto_info.iv, iv + 4, 8);
            std::memcpy(crypto_info.salt, iv, 4);
            std::memcpy(crypto_info.key, key, 16);
            std::memset(crypto_info.rec_seq, 0, 8);
            int tx_ret = ::setsockopt(client_fd_, 282 /* SOL_TLS */, 1 /* TLS_TX */, &crypto_info, sizeof(crypto_info));
            if (tx_ret == 0) ktls_tx_ = true;
            OPENSSL_cleanse(&crypto_info, sizeof(crypto_info));
        } else {
            struct tls12_crypto_info_aes_gcm_256 crypto_info{};
            crypto_info.info.version = 0x0304; // TLS_1_3_VERSION
            crypto_info.info.cipher_type = cipher_type;
            std::memcpy(crypto_info.iv, iv + 4, 8);
            std::memcpy(crypto_info.salt, iv, 4);
            std::memcpy(crypto_info.key, key, 32);
            std::memset(crypto_info.rec_seq, 0, 8);
            int tx_ret = ::setsockopt(client_fd_, 282 /* SOL_TLS */, 1 /* TLS_TX */, &crypto_info, sizeof(crypto_info));
            if (tx_ret == 0) ktls_tx_ = true;
            OPENSSL_cleanse(&crypto_info, sizeof(crypto_info));
        }
    }

    OPENSSL_cleanse(secret, sizeof(secret));
    OPENSSL_cleanse(key, sizeof(key));
    OPENSSL_cleanse(iv, sizeof(iv));
    server_traffic_secret_.clear();
    return ktls_tx_;
#endif
    return false;
}

core::Task<bool> TlsStream::handshake() {
    while (!SSL_is_init_finished(ssl_)) {
        int ret = SSL_accept(ssl_);

        bool flushed = co_await flush_outbound();
        if (!flushed) co_return false;

        if (ret == 1) {
            handshake_done_ = true;
            (void)try_enable_ktls_tx();
            co_return true;
        }

        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ) {
            auto recv_res = co_await loop_.engine().recv_provided(client_fd_, loop_.buffer_pool().bgid());
            if (recv_res.bytes <= 0) {
                co_return false;
            }

            auto slice = loop_.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
            BIO_write(in_bio_, slice.data(), static_cast<int>(slice.size()));
            loop_.buffer_pool().return_buffer(recv_res.bid);
        } else if (err == SSL_ERROR_WANT_WRITE) {
            // Already flushed
        } else {
            co_return false;
        }
    }

    handshake_done_ = true;
    (void)try_enable_ktls_tx();
    co_return true;
}

core::Task<int> TlsStream::read_plaintext(void* buf, size_t max_len) {
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
            auto recv_res = co_await loop_.engine().recv_provided(client_fd_, loop_.buffer_pool().bgid());
            if (recv_res.bytes <= 0) {
                co_return 0; // EOF
            }

            auto slice = loop_.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
            BIO_write(in_bio_, slice.data(), static_cast<int>(slice.size()));
            loop_.buffer_pool().return_buffer(recv_res.bid);
        } else if (err == SSL_ERROR_WANT_WRITE) {
            bool flushed = co_await flush_outbound();
            if (!flushed) {
                co_return -1;
            }
        } else {
            co_return -1;
        }
    }
}

core::Task<int> TlsStream::write_plaintext(const void* buf, size_t len) {
    if (ktls_tx_) {
        int sent = co_await loop_.engine().send_all(client_fd_, buf, len);
        if (sent != static_cast<int>(len)) {
            co_return -1;
        }
        co_return sent;
    }

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

std::string_view TlsStream::alpn() const noexcept {
    if (!ssl_ || !handshake_done_) return "";
    const unsigned char* proto = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(ssl_, &proto, &len);
    if (!proto || len == 0) return "";
    return std::string_view(reinterpret_cast<const char*>(proto), len);
}

} // namespace aegon::http::tls
