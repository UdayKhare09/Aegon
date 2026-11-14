#pragma once

#include "core/EventLoop.h"
#include "core/OsSocket.h"
#include "core/Task.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace aegon::net::tls {

/**
 * @brief High-performance asynchronous TLS stream wrapper using memory BIOs and io_uring.
 * Provides full support for Kernel TLS (kTLS) TX offload and ALPN negotiation.
 */
class TlsStream {
public:
    TlsStream(core::EventLoop& loop, core::os_socket_t client_fd, SSL_CTX* ctx);
    ~TlsStream();

    TlsStream(const TlsStream&) = delete;
    TlsStream& operator=(const TlsStream&) = delete;

    TlsStream(TlsStream&& other) noexcept;
    TlsStream& operator=(TlsStream&& other) noexcept;

    /**
     * @brief Perform async TLS handshake over io_uring.
     * @return true if handshake completed successfully, false on error/disconnect.
     */
    core::Task<bool> handshake();

    /**
     * @brief Read decrypted application plaintext from the TLS connection.
     */
    core::Task<int> read_plaintext(void* buf, size_t max_len);

    /**
     * @brief Encrypt and transmit application plaintext over io_uring.
     */
    core::Task<int> write_plaintext(const void* buf, size_t len);

    /**
     * Encrypts (or, with kTLS, hands to the kernel) and sends `len` bytes now, without
     * suspending. Whatever the socket does not take stays queued: when has_pending_output()
     * is true, co_await drain() before writing again. False on a connection error.
     */
    bool write_now(const void* buf, size_t len);
    [[nodiscard]] bool has_pending_output() const noexcept { return io_ && !io_->tx_pending.empty(); }
    core::Task<bool> drain() { return flush_outbound(); }

    /**
     * @brief Negotiated ALPN protocol (e.g. "h2", "http/1.1", or custom protocol).
     */
    [[nodiscard]] std::string_view alpn() const noexcept;

    [[nodiscard]] bool is_h2() const noexcept { return alpn() == "h2"; }

    /**
     * @brief Whether Kernel TLS (kTLS) TX offload is active for outbound writes.
     */
    [[nodiscard]] bool is_ktls_tx() const noexcept { return ktls_tx_; }

    /** Whether kernel TLS TX may be enabled after the handshake (off by default). */
    void allow_ktls(bool allow) noexcept { ktls_allowed_ = allow; }

    /**
     * @brief Captures server traffic secret from TLS 1.3 keylog callback for kTLS programming.
     */
    void set_server_traffic_secret(std::string_view hex_secret) {
        server_traffic_secret_ = hex_secret;
    }

    [[nodiscard]] core::os_socket_t native_handle() const noexcept { return client_fd_; }
    [[nodiscard]] SSL* native_ssl() const noexcept { return ssl_; }

private:
    core::Task<bool> flush_outbound();
    bool try_enable_ktls_tx();
    // Sends `len` bytes: a direct send from the loop thread first, the ring only for the rest.
    core::Task<int> send_direct(const void* buf, size_t len);

public:
    // State behind the connection's BIO (see TlsStream.cpp). OpenSSL reads ciphertext
    // straight out of the current receive buffer (`rx`); bytes it has not consumed when that
    // buffer goes back to the ring move to `carry`. Records OpenSSL writes are sent from its
    // own buffer with a direct send(); only what the socket refuses lands in `tx_pending`.
    struct BioIo {
        core::os_socket_t fd{core::INVALID_OS_SOCKET};
        const uint8_t* rx{nullptr};
        size_t rx_len{0};
        std::string carry;
        size_t carry_off{0};
        std::string tx_pending;
        bool tx_error{false};

        // Moves what is left of `rx` into `carry` (the receive buffer is about to be returned).
        void stash_rx();
    };

private:
    core::EventLoop* loop_{nullptr};
    core::os_socket_t client_fd_{core::INVALID_OS_SOCKET};
    SSL* ssl_{nullptr};
    std::unique_ptr<BioIo> io_{};  // heap: the BIO points at it, so a moved TlsStream stays valid
    bool handshake_done_{false};
    bool ktls_tx_{false};
    bool ktls_allowed_{false};
    std::string server_traffic_secret_{};
    // After the handshake, ciphertext arrives through one armed multishot recv instead of a
    // one-shot recv per read.
    std::optional<core::MultishotRecvStream> rx_{};
};

int get_tls_stream_ex_index() noexcept;

} // namespace aegon::net::tls

