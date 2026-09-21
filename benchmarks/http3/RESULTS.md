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

## 4. Run-by-Run Detailed Triplicate Breakdown

### Aegon (C++26)
| Workload | Run # | Throughput (req/s) | Mean Latency | p50 Latency | p99 Latency | Total Requests |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Plaintext** | Run 1 | 330,122.30 | 3.03 ms | 2.67 ms | 5.45 ms | 3,301,223 |
| | Run 2 | 341,262.40 | 2.95 ms | 2.71 ms | 5.12 ms | 3,412,624 |
| | Run 3 | 337,937.50 | 2.97 ms | 2.61 ms | 4.88 ms | 3,379,375 |
| **JSON** | Run 1 | 327,705.80 | 3.06 ms | 2.72 ms | 5.85 ms | 3,277,058 |
| | Run 2 | 336,284.60 | 2.98 ms | 2.72 ms | 5.31 ms | 3,362,846 |
| | Run 3 | 330,833.70 | 3.01 ms | 2.65 ms | 5.08 ms | 3,308,337 |
| **User Post** | Run 1 | 328,177.30 | 3.06 ms | 2.82 ms | 5.23 ms | 3,281,773 |
| | Run 2 | 334,699.10 | 2.99 ms | 2.68 ms | 5.21 ms | 3,346,991 |
| | Run 3 | 317,171.50 | 3.25 ms | 2.83 ms | 7.73 ms | 3,171,715 |

### Swerver (Zig)
| Workload | Run # | Throughput (req/s) | Mean Latency | p50 Latency | p99 Latency | Total Requests |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Plaintext** | Run 1 | 108,680.70 | 9.20 ms | 7.40 ms | 27.37 ms | 1,086,807 |
| | Run 2 | 109,883.70 | 9.09 ms | 7.25 ms | 31.62 ms | 1,098,837 |
| | Run 3 | 110,147.40 | 9.06 ms | 7.28 ms | 22.40 ms | 1,101,474 |
| **JSON** | Run 1 | 109,000.60 | 9.16 ms | 7.38 ms | 23.26 ms | 1,090,006 |
| | Run 2 | 109,284.20 | 9.15 ms | 7.35 ms | 23.33 ms | 1,092,842 |
| | Run 3 | 109,742.70 | 9.10 ms | 7.27 ms | 28.19 ms | 1,097,427 |
| **User Post** | Run 1 | 108,039.80 | 9.25 ms | 7.42 ms | 23.11 ms | 1,080,398 |
| | Run 2 | 109,022.00 | 9.16 ms | 7.32 ms | 28.10 ms | 1,090,220 |
| | Run 3 | 107,352.00 | 9.29 ms | 7.44 ms | 26.21 ms | 1,073,520 |

---

## 5. Architectural Deep Dive: Why Aegon Leads HTTP/3 Performance

1. **Zero-Copy UDP Batching via Linux `sendmmsg`/`recvmmsg` and `io_uring`**:
   - HTTP/3 transports all data via UDP datagrams. Under 1,000 active streams, issuing single `sendto` / `recvfrom` syscalls per datagram introduces massive kernel context switch overhead.
   - Aegon batches outgoing UDP packets into contiguous vectors using `sendmmsg` and `io_uring` batched completions, minimizing kernel boundary transitions.

2. **RFC 9000 Stream Credit Management**:
   - Aegon actively manages QUIC transport parameters and stream credits via `ngtcp2_conn_extend_max_streams_bidi(conn, 1)` upon every stream closure. This ensures instantaneous stream replenishment without client stall.

3. **In-Register SimdRouter & Zero-Allocation QPACK Encoding**:
   - Aegon couples the `nghttp3` QPACK compressor with static table pre-indexing and `glaze` compile-time JSON serialization, avoiding heap allocations in the hot request/response loop.

4. **Lockless UDP Socket Multiplexing with `SO_REUSEPORT`**:
   - Both worker threads independently bind to the UDP port using `SO_REUSEPORT`, allowing the Linux kernel's UDP hash steering to route QUIC 4-tuples directly to dedicated thread rings without inter-thread locks.
