#pragma once

#include <cstddef>
#include <cstdint>

namespace aegon::http {

/**
 * @brief RFC compliance and protocol payload limits across H1, H2, and H3.
 */
struct ProtocolLimits {
    size_t max_uri_length{8192};            // 8 KB limit -> 414 URI Too Long
    size_t max_headers_size{65536};         // 64 KB total header section limit -> 431 Request Header Fields Too Large
    size_t max_body_size{16 * 1024 * 1024}; // 16 MB maximum payload size -> 413 Payload Too Large
};

/**
 * @brief Operating system TCP socket options.
 */
struct TcpConfig {
    bool nodelay{true};
    bool keepalive{true};
    int keepidle{30};  // Seconds before sending keepalive probes
    int keepintvl{10}; // Seconds between keepalive probes
    int keepcnt{3};    // Dropped connection after N missed probes
};

/**
 * @brief Unified configuration object for Aegon Server and all protocol engines.
 * Holds all tunable parameters for io_uring, socket options, protocol limits,
 * HTTP/2 settings, and flood mitigations.
 */
struct ServerConfig {
    // Protocol payload & compliance limits
    ProtocolLimits limits{};

    // Linux io_uring & Provided Buffer Pool
    uint32_t ring_entries{4096};
    uint16_t buffer_pool_entries{8192};
    uint16_t buffer_size{4096};

    // TCP Socket Options
    TcpConfig tcp{};

    // HTTP/2 & HTTP/3 Flow Control & Resource Tuning
    uint32_t h2_max_concurrent_streams{256};
    uint32_t h2_initial_window_size{1048576}; // 1 MB initial window
    uint32_t rst_burst_limit{1000};           // Rapid Reset flood threshold

    // Protocol Feature Toggles
    bool enable_http3{true}; // Enabled automatically when TLS is enabled
};

} // namespace aegon::http
