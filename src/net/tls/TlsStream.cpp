#include "net/tls/TlsStream.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#if defined(__linux__)
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <linux/tls.h>
#endif
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <cstdlib>

namespace aegon::net::tls {

int get_tls_stream_ex_index() noexcept {
    static int idx = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return idx;
}

namespace {

int bio_io_read(BIO* b, char* out, int len) {
    auto* io = static_cast<TlsStream::BioIo*>(BIO_get_data(b));
    BIO_clear_retry_flags(b);
    if (len <= 0) return 0;
    size_t want = static_cast<size_t>(len), n = 0;
    if (io->carry_off < io->carry.size()) {
        size_t k = std::min(want, io->carry.size() - io->carry_off);
        std::memcpy(out, io->carry.data() + io->carry_off, k);
        io->carry_off += k;
        n += k;
        if (io->carry_off == io->carry.size()) {
            io->carry.clear();
            io->carry_off = 0;
        }
    }
    if (n < want && io->rx_len > 0) {
        size_t k = std::min(want - n, io->rx_len);
        std::memcpy(out + n, io->rx, k);
        io->rx += k;
        io->rx_len -= k;
        n += k;
    }
    if (n == 0) {
        BIO_set_retry_read(b);
        return -1;
    }
    return static_cast<int>(n);
}

int bio_io_write(BIO* b, const char* in, int len) {
    auto* io = static_cast<TlsStream::BioIo*>(BIO_get_data(b));
    BIO_clear_retry_flags(b);
    if (len <= 0) return 0;
    if (io->tx_error) return -1;
    if (io->tx_pending.empty()) {
        // Straight from OpenSSL's record buffer to the socket.
        int sent = core::IoEngine::send_now(io->fd, in, static_cast<size_t>(len));
        if (sent == len) return len;
        if (sent < 0 && sent != -EAGAIN && sent != -EWOULDBLOCK && sent != -EINTR) {
            io->tx_error = true;
            return -1;
        }
        size_t done = sent > 0 ? static_cast<size_t>(sent) : 0;
        io->tx_pending.append(in + done, static_cast<size_t>(len) - done);
        return len;
    }
    io->tx_pending.append(in, static_cast<size_t>(len));  // keep record order behind what is queued
    return len;
}

long bio_io_ctrl(BIO* b, int cmd, long, void*) {
    auto* io = static_cast<TlsStream::BioIo*>(BIO_get_data(b));
    switch (cmd) {
        case BIO_CTRL_FLUSH:
        case BIO_CTRL_DUP:
            return 1;
        case BIO_CTRL_PENDING:
            return io ? static_cast<long>(io->rx_len + (io->carry.size() - io->carry_off)) : 0;
        case BIO_CTRL_WPENDING:
            return io ? static_cast<long>(io->tx_pending.size()) : 0;
        default:
            return 0;
    }
}

int bio_io_create(BIO* b) {
    BIO_set_init(b, 1);
    return 1;
}

int bio_io_destroy(BIO*) { return 1; }

BIO_METHOD* bio_io_method() {
    static BIO_METHOD* method = [] {
        BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "aegon-io");
        BIO_meth_set_write(m, bio_io_write);
        BIO_meth_set_read(m, bio_io_read);
        BIO_meth_set_ctrl(m, bio_io_ctrl);
        BIO_meth_set_create(m, bio_io_create);
        BIO_meth_set_destroy(m, bio_io_destroy);
        return m;
    }();
    return method;
}

} // namespace

void TlsStream::BioIo::stash_rx() {
    if (rx_len == 0) return;
    if (carry_off > 0) {
        carry.erase(0, carry_off);
        carry_off = 0;
    }
    carry.append(reinterpret_cast<const char*>(rx), rx_len);
    rx = nullptr;
    rx_len = 0;
}

TlsStream::TlsStream(core::EventLoop& loop, core::os_socket_t client_fd, SSL_CTX* ctx)
    : loop_(&loop), client_fd_(client_fd), io_(std::make_unique<BioIo>()) {
    io_->fd = client_fd;
    ssl_ = SSL_new(ctx);
    BIO* bio = BIO_new(bio_io_method());
    BIO_set_data(bio, io_.get());
    SSL_set_bio(ssl_, bio, bio);  // one BIO for both directions; SSL owns it
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
      ssl_(other.ssl_), io_(std::move(other.io_)),
      handshake_done_(other.handshake_done_), ktls_tx_(other.ktls_tx_), ktls_allowed_(other.ktls_allowed_),
      server_traffic_secret_(std::move(other.server_traffic_secret_)),
      rx_(std::move(other.rx_)) {
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
        io_ = std::move(other.io_);
        handshake_done_ = other.handshake_done_;
        ktls_tx_ = other.ktls_tx_;
        ktls_allowed_ = other.ktls_allowed_;
        server_traffic_secret_ = std::move(other.server_traffic_secret_);
        rx_ = std::move(other.rx_);

        other.ssl_ = nullptr;
        other.client_fd_ = core::INVALID_OS_SOCKET;
        if (ssl_) {
            SSL_set_ex_data(ssl_, get_tls_stream_ex_index(), this);
        }
    }
    return *this;
}

core::Task<int> TlsStream::send_direct(const void* buf, size_t len) {
    int sent = core::IoEngine::send_now(client_fd_, buf, len);
    if (sent == static_cast<int>(len)) [[likely]] {
        co_return sent;
    }
    if (sent < 0 && sent != -EAGAIN && sent != -EWOULDBLOCK && sent != -EINTR) {
        co_return sent;
    }
    size_t done = sent > 0 ? static_cast<size_t>(sent) : 0;
    int rem = co_await loop_->engine().send_all(
        client_fd_, std::span<const uint8_t>(static_cast<const uint8_t*>(buf) + done, len - done));
    co_return rem < 0 ? rem : static_cast<int>(done) + rem;
}

core::Task<bool> TlsStream::flush_outbound() {
    // Records normally left with a direct send inside the BIO; only a backlog is sent here.
    if (io_->tx_error) co_return false;
    if (!io_->tx_pending.empty()) {
        std::string pending;
        pending.swap(io_->tx_pending);
        int sent = co_await loop_->engine().send_all(client_fd_, std::string_view(pending));
        if (sent != static_cast<int>(pending.size())) {
            io_->tx_error = true;
            co_return false;
        }
    }
    co_return true;
}

bool TlsStream::try_enable_ktls_tx() {
    if (!ktls_allowed_ || server_traffic_secret_.empty() || client_fd_ < 0) return false;

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
#else
    return false;
#endif
}

core::Task<bool> TlsStream::handshake() {
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
            auto recv_res = co_await loop_->engine().recv_provided(client_fd_, loop_->buffer_pool().bgid());
            if (recv_res.bytes == -ENOBUFS) {
                co_await loop_->engine().timeout(100'000ULL);
                continue;
            }
            if (recv_res.bytes <= 0) {
                co_return false;
            }

            // Handshake flights are rare: keep it simple and copy into the carry.
            auto slice = loop_->buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
            io_->carry.append(reinterpret_cast<const char*>(slice.data()), slice.size());
            loop_->buffer_pool().return_buffer(recv_res.bid);
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
    int held_bid = -1;  // receive buffer OpenSSL is reading from, returned after each SSL call
    while (true) {
        size_t got = 0;
        int ok = SSL_read_ex(ssl_, buf, max_len, &got);
        if (held_bid >= 0) {
            io_->stash_rx();
            loop_->buffer_pool().return_buffer(static_cast<uint16_t>(held_bid));
            held_bid = -1;
        }
        if (ok == 1) {
            co_return static_cast<int>(got);
        }

        int err = SSL_get_error(ssl_, 0);
        if (err == SSL_ERROR_ZERO_RETURN) {
            co_return 0; // Clean TLS shutdown
        }
        // Anything OpenSSL wrote while reading (alerts, key updates) that is still queued.
        if (!co_await flush_outbound()) {
            co_return -1;
        }
        if (err != SSL_ERROR_WANT_READ) {
            co_return -1;
        }

        if (!rx_) {
            rx_.emplace(loop_->engine().recv_multishot_stream(client_fd_, loop_->buffer_pool().bgid()));
        }
        auto recv_res = co_await rx_->next();
        if (recv_res.bytes <= 0 || !recv_res.buffer_valid) {
            co_return 0; // EOF
        }
        auto slice = loop_->buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
        io_->rx = slice.data();
        io_->rx_len = slice.size();
        held_bid = recv_res.bid;
    }
}

core::Task<int> TlsStream::write_plaintext(const void* buf, size_t len) {
    if (len == 0) co_return 0;

    if (ktls_tx_) {
        // kTLS TX offload: the kernel encrypts on send.
        co_return co_await send_direct(buf, len);
    }

    size_t written = 0;
    if (SSL_write_ex(ssl_, buf, len, &written) != 1) {
        co_return -1;
    }
    // The records already went out from inside the BIO unless the socket was full.
    if (!co_await flush_outbound()) {
        co_return -1;
    }
    co_return static_cast<int>(written);
}

bool TlsStream::write_now(const void* buf, size_t len) {
    if (len == 0) return true;
    if (io_->tx_error) return false;
    if (ktls_tx_) {
        if (!io_->tx_pending.empty()) {
            io_->tx_pending.append(static_cast<const char*>(buf), len);  // keep order
            return true;
        }
        int sent = core::IoEngine::send_now(client_fd_, buf, len);
        if (sent == static_cast<int>(len)) return true;
        if (sent < 0 && sent != -EAGAIN && sent != -EWOULDBLOCK && sent != -EINTR) {
            io_->tx_error = true;
            return false;
        }
        size_t done = sent > 0 ? static_cast<size_t>(sent) : 0;
        io_->tx_pending.append(static_cast<const char*>(buf) + done, len - done);
        return true;
    }
    // Records go out from inside the BIO (direct send); the rest is queued in tx_pending.
    size_t written = 0;
    return SSL_write_ex(ssl_, buf, len, &written) == 1 && written == len && !io_->tx_error;
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

