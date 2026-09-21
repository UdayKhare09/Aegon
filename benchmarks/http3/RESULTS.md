# HTTP/3 over QUIC Benchmark Results (RFC 9000 / RFC 9114)

This benchmark suite evaluates native **HTTP/3 over QUIC** performance across modern high-performance web frameworks under rigorous scientific isolation.

---

## 1. System Specifications & Scientific Methodology

### Hardware & Operating System
- **Processor**: AMD Ryzen 5 7600X (6 Zen 4 physical cores / 12 logical threads)
- **Host OS**: Linux 6.18.2-zen2-1-zen (x86_64)
- **Network Interface**: Linux loopback (`lo`, MTU 65536)
- **QUIC / TLS Stack**: OpenSSL 3.6.4, ngtcp2 1.25.0, nghttp3 1.18.0

### CPU Core Isolation Topology
To eliminate CPU cache thrashing, context switching jitter, and client-server core contention:
- **Server Processes**: Strictly pinned to **Physical Cores 0, 1** (`taskset -c 0,1`, 2 worker threads).
- **Client Generator**: Strictly pinned to **Physical Cores 2, 3, 4, 5** (`taskset -c 2,3,4,5`, 4 client threads).
- **Load Generator**: Custom `h2load` compiled from `nghttp2 v1.70.90` with native HTTP/3 over QUIC support (`--h3`) linked against `libngtcp2`, `libnghttp3`, and `libngtcp2_crypto_ossl`.

### Test Parameters & Rigor
- **Protocol**: HTTP/3 over QUIC (RFC 9000 / RFC 9114, ALPN `h3`)
- **Transport**: UDP Datagrams with TLS 1.3 cryptographic protection
- **Concurrency**: 100 concurrent QUIC connections × 10 concurrent streams/conn = **1,000 active in-flight multiplexed streams**.
- **Triplicate Methodology**: Each test underwent a **3-second warm-up** followed by **3 consecutive 10-second measurement runs** with cooldown periods. All reported metrics represent un-cherry-picked arithmetic averages across all three runs.

---

## 2. Framework Support & Scope

| Framework | Language | HTTP/3 Engine | RFC 9000 / 9114 Status | Notes |
| :--- | :---: | :---: | :---: | :--- |
| **Aegon** | C++26 | `io_uring` + `ngtcp2` + `nghttp3` | **SUPPORTED** | Native coroutines (`std::coroutine`), batch UDP datagram I/O. |
| **Swerver** | Zig | `io_uring` + OpenSSL QUIC + Native H3 | **SUPPORTED** | Native Zig QUIC state machine + QPACK decompression. |
| **Actix-web** | Rust | N/A | **UNSUPPORTED** | `actix-server` and `actix-http` only implement TCP streams; no QUIC/UDP socket engine. |
| **Drogon** | C++ | N/A | **UNSUPPORTED** | Trantor network reactor is strictly TCP `epoll`; no QUIC framing or UDP packet processing. |
| **Fiber** | Go | N/A | **UNSUPPORTED** | `fasthttp` core is hardcoded to `net.Listener` / TCP streams; no QUIC integration. |

---

## 3. Executive Summary (Triplicate Averages)

### Workload 1: Plaintext (`/plaintext` - TechEmpower Standard)
> Payload: `Hello, World!` (`text/plain`, 13 bytes)

| Framework | Throughput (req/s) | Total Requests (10s) | Mean Latency | p50 Latency | p99 Latency | Error Rate |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Aegon (C++26)** | **336,440.73** | **3,364,407** | **2.98 ms** | **2.66 ms** | **5.15 ms** | **0.00%** |
| **Swerver (Zig)** | 109,570.60 | 1,095,706 | 9.12 ms | 7.31 ms | 27.13 ms | 0.00% |
| *Actix-web (Rust)* | *UNSUPPORTED* | - | - | - | - | - |
| *Drogon (C++)* | *UNSUPPORTED* | - | - | - | - | - |
| *Fiber (Go)* | *UNSUPPORTED* | - | - | - | - | - |

- **Throughput Advantage**: Aegon is **3.07× faster** than Swerver.
- **Latency Advantage**: Aegon provides **3.06× lower mean latency** and **5.27× lower p99 tail latency**.

---

### Workload 2: JSON Serialization (`/json`)
> Dynamic JSON payload generated per request: `{"message":"Hello, World!","id":"<UUIDv4>","timestamp":<microsecond_epoch>}`

| Framework | Throughput (req/s) | Total Requests (10s) | Mean Latency | p50 Latency | p99 Latency | Error Rate |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Aegon (C++26)** | **331,608.03** | **3,316,080** | **3.02 ms** | **2.70 ms** | **5.41 ms** | **0.00%** |
| **Swerver (Zig)** | 109,342.50 | 1,093,425 | 9.14 ms | 7.33 ms | 24.93 ms | 0.00% |
| *Actix-web (Rust)* | *UNSUPPORTED* | - | - | - | - | - |
| *Drogon (C++)* | *UNSUPPORTED* | - | - | - | - | - |
| *Fiber (Go)* | *UNSUPPORTED* | - | - | - | - | - |

- **Throughput Advantage**: Aegon is **3.03× faster** than Swerver.
- **Latency Advantage**: Aegon provides **3.03× lower mean latency** and **4.61× lower p99 tail latency**.

---

### Workload 3: Dynamic Path Routing (`/users/42/posts/101`)
> Two variable path parameters extracted, parsed as integers, and returned in a JSON object: `{"user_id":42,"post_id":101}`

| Framework | Throughput (req/s) | Total Requests (10s) | Mean Latency | p50 Latency | p99 Latency | Error Rate |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Aegon (C++26)** | **326,682.63** | **3,266,826** | **3.10 ms** | **2.78 ms** | **6.06 ms** | **0.00%** |
| **Swerver (Zig)** | 108,137.93 | 1,081,379 | 9.23 ms | 7.39 ms | 25.81 ms | 0.00% |
| *Actix-web (Rust)* | *UNSUPPORTED* | - | - | - | - | - |
| *Drogon (C++)* | *UNSUPPORTED* | - | - | - | - | - |
| *Fiber (Go)* | *UNSUPPORTED* | - | - | - | - | - |

- **Throughput Advantage**: Aegon is **3.02× faster** than Swerver.
- **Latency Advantage**: Aegon provides **2.98× lower mean latency** and **4.26× lower p99 tail latency**.

---

## 4. Architectural Deep Dive: Why Aegon Leads HTTP/3 Performance

1. **Zero-Copy UDP Batching via Linux `sendmmsg`/`recvmmsg` and `io_uring`**:
   - HTTP/3 transports all data via UDP datagrams. Under 1,000 active streams, issuing single `sendto` / `recvfrom` syscalls per datagram introduces massive kernel context switch overhead.
   - Aegon batches outgoing UDP packets into contiguous vectors using `sendmmsg` and `io_uring` batched completions, minimizing kernel boundary transitions.

2. **RFC 9000 Stream Credit Management**:
   - Aegon actively manages QUIC transport parameters and stream credits via `ngtcp2_conn_extend_max_streams_bidi(conn, 1)` upon every stream closure. This ensures instantaneous stream replenishment without client stall.

3. **In-Register SimdRouter & Zero-Allocation QPACK Encoding**:
   - Aegon couples the `nghttp3` QPACK compressor with static table pre-indexing and `glaze` compile-time JSON serialization, avoiding heap allocations in the hot request/response loop.

4. **Lockless UDP Socket Multiplexing with `SO_REUSEPORT`**:
   - Both worker threads independently bind to the UDP port using `SO_REUSEPORT`, allowing the Linux kernel's UDP hash steering to route QUIC 4-tuples directly to dedicated thread rings without inter-thread locks.
