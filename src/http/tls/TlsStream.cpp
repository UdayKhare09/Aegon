#include "http/tls/TlsStream.h"
#include <iostream>

namespace aegon::http::tls {

TlsStream::TlsStream(core::EventLoop& loop, int client_fd, SSL_CTX* ctx)
    : loop_(loop), client_fd_(client_fd) {
    ssl_ = SSL_new(ctx);
    in_bio_ = BIO_new(BIO_s_mem());
    out_bio_ = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl_, in_bio_, out_bio_);
    SSL_set_accept_state(ssl_);
}

TlsStream::~TlsStream() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
}

core::Task<bool> TlsStream::flush_outbound() {
    char out_buf[4096];
    while (BIO_pending(out_bio_) > 0) {
        int n = BIO_read(out_bio_, out_buf, sizeof(out_buf));
        if (n > 0) {
            int sent = co_await loop_.ring().send_all(
                client_fd_,
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(out_buf), static_cast<size_t>(n)));
            if (sent <= 0) {
                co_return false;
            }
        }
    }
    co_return true;
}

core::Task<bool> TlsStream::handshake() {
    while (!SSL_is_init_finished(ssl_)) {
        int ret = SSL_accept(ssl_);

        bool flushed = co_await flush_outbound();
        if (!flushed) co_return false;

        if (ret == 1) {
            handshake_done_ = true;
            co_return true;
        }

        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ) {
            auto recv_res = co_await loop_.ring().recv_multishot(client_fd_, loop_.buffer_pool().bgid());
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
            auto recv_res = co_await loop_.ring().recv_multishot(client_fd_, loop_.buffer_pool().bgid());
            if (recv_res.bytes <= 0) {
                co_return 0; // EOF
            }

            auto slice = loop_.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
            BIO_write(in_bio_, slice.data(), static_cast<int>(slice.size()));
            loop_.buffer_pool().return_buffer(recv_res.bid);
        } else {
            co_return -1;
        }
    }
}

core::Task<int> TlsStream::write_plaintext(const void* buf, size_t len) {
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
