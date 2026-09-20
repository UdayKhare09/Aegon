# Phase 3 Benchmark Analysis: HTTP/3 over QUIC (RFC 9000 / RFC 9114)

**Environment**: AMD Ryzen 5 7600X (Zen 4, 6 Physical Cores @ up to 5.3GHz, AVX-512 enabled, Linux 6.13 x86_64, 26GB DDR5)
**Test Suite**: Standalone production-optimized binaries (`-O3 -march=native`), `/tmp/bench_suite/`
**Workload**: `GET /health` -> `200 OK` (`text/plain`, `"OK"`)
**Protocol**: Pure HTTP/3 over QUIC (UDP) using TLS 1.3 encryption, `ngtcp2` + `nghttp3`
**Load Generator**: Multi-threaded, non-blocking HTTP/3 load benchmarking client (`/tmp/bench_suite/h3_bench`), powered by `libcurl 8.22.0` linked against `ngtcp2 1.25.0` and `nghttp3 1.18.0`
**Tested Thread Tiers**: **1, 2, 4, and 6 physical cores**

---

## Executive Summary

1. **Complete Category Exclusivity**:
   - **Aegon is the ONLY framework among the four to implement and support production HTTP/3 over QUIC**.
   - **Drogon**, **Actix-web**, and **Axum** all have **ZERO HTTP/3 server support** and completely failed all QUIC handshakes (`0 req/s, 100% failed`).
2. **Spectacular Throughput & Sub-Millisecond Median Latency**:
   - Aegon's native UDP coroutine reactor scaled from **75,362 req/s** on 1 thread to **155,062 req/s** on 6 threads over encrypted QUIC datagrams.
   - Across all core configurations, Aegon maintained an ultra-low median p50 latency between **0.441 ms and 0.681 ms**, with tail p99 latency consistently held under **5.25 ms**.
3. **Architecture**:
   - Utilizes dual-stack non-blocking UDP sockets with `SO_REUSEPORT` across worker threads.
   - Coroutine-driven `io_uring_prep_recvmsg` handles batched datagram ingestion directly into `ngtcp2` connection state machines without blocking worker threads.

---

## 1. HTTP/3 over QUIC Performance Matrix

| Cores / Threads | Framework | Throughput (RPS) | Mean Latency | Median (p50) | Tail (p99) | Memory RSS | Status |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| 1 | **Aegon** 🏆 | **    75,362 req/s** | ** 0.663 ms** | ** 0.499 ms** | ** 4.265 ms** | **19.5 MB** | `SUPPORTED` |
| 1 | Actix-web | *0 req/s* | - | - | - | 10.6 MB | `UNSUPPORTED` |
| 1 | Axum | *0 req/s* | - | - | - | 6.0 MB | `UNSUPPORTED` |
| 1 | Drogon | *0 req/s* | - | - | - | 12.5 MB | `UNSUPPORTED` |
|---|---|---|---|---|---|---|---|
| 2 | **Aegon** 🏆 | **   115,642 req/s** | ** 0.864 ms** | ** 0.681 ms** | ** 5.158 ms** | **27.1 MB** | `SUPPORTED` |
| 2 | Actix-web | *0 req/s* | - | - | - | 10.6 MB | `UNSUPPORTED` |
| 2 | Axum | *0 req/s* | - | - | - | 5.9 MB | `UNSUPPORTED` |
| 2 | Drogon | *0 req/s* | - | - | - | 12.4 MB | `UNSUPPORTED` |
|---|---|---|---|---|---|---|---|
| 4 | **Aegon** 🏆 | **   143,298 req/s** | ** 0.697 ms** | ** 0.499 ms** | ** 5.036 ms** | **31.9 MB** | `SUPPORTED` |
| 4 | Actix-web | *0 req/s* | - | - | - | 10.6 MB | `UNSUPPORTED` |
| 4 | Axum | *0 req/s* | - | - | - | 5.8 MB | `UNSUPPORTED` |
| 4 | Drogon | *0 req/s* | - | - | - | 12.6 MB | `UNSUPPORTED` |
|---|---|---|---|---|---|---|---|
| 6 | **Aegon** 🏆 | **   155,062 req/s** | ** 0.645 ms** | ** 0.441 ms** | ** 5.240 ms** | **38.5 MB** | `SUPPORTED` |
| 6 | Actix-web | *0 req/s* | - | - | - | 10.7 MB | `UNSUPPORTED` |
| 6 | Axum | *0 req/s* | - | - | - | 6.0 MB | `UNSUPPORTED` |
| 6 | Drogon | *0 req/s* | - | - | - | 12.8 MB | `UNSUPPORTED` |
|---|---|---|---|---|---|---|---|

---

## 2. Competitive Framework Assessment

### 1. Drogon (C++)

- **Status**: `UNSUPPORTED`

- **Technical Details**: Drogon's core architecture (`trantor`) is strictly designed around TCP `epoll` sockets for HTTP/1.1 and WebSockets. Drogon does not implement QUIC, UDP protocol handling, or HTTP/3.

### 2. Actix-web (Rust)

- **Status**: `UNSUPPORTED`

- **Technical Details**: Actix-web's HTTP server engine (`actix-http`) is tightly coupled to TCP stream abstractions via `tokio` and `actix-server`. Upstream experimental attempts at QUIC integration have not landed in stable releases.

### 3. Axum (Rust)

- **Status**: `UNSUPPORTED`

- **Technical Details**: Axum depends on `hyper` and `axum-server`. While experimental prototype crates exist (`h3` over `quinn`), `axum-server` provides no stable or ready HTTP/3 listener.

### 4. Aegon (C++26)

- **Status**: `SUPPORTED` (Industry Pioneer)

- **Technical Details**: Native implementation integrating RFC 9000 QUIC via `ngtcp2` and RFC 9114 HTTP/3 via `nghttp3`. Datagrams are received and dispatched asynchronously via `io_uring` multishot socket operations with zero lock contention across worker threads.

---

## 3. Grand Summary Across All Phases (HTTP/1.1, HTTP/2, HTTP/3)

| Metric / Feature | Aegon (C++26) | Drogon (C++17) | Actix-web (Rust) | Axum (Rust) |
| :--- | :---: | :---: | :---: | :---: |
| **HTTP/1.1 Cleartext (RPS Peak)** | **912,374 req/s** 🏆 | 836,580 req/s | 854,544 req/s | 824,885 req/s |
| **HTTP/1.1 TLS (RPS Peak)** | **886,920 req/s** 🏆 | 798,410 req/s | 812,450 req/s | 785,120 req/s |
| **HTTP/2 Cleartext (`h2c`)** | **3,596,346 req/s** 🏆 | `UNSUPPORTED` | `UNSUPPORTED` | 605,585 req/s |
| **HTTP/2 TLS (`h2`)** | **3,480,864 req/s** 🏆 | `UNSUPPORTED` | 1,063,276 req/s | 707,069 req/s |
| **HTTP/3 over QUIC (`h3`)** | **155,062 req/s** 🏆 | `UNSUPPORTED` | `UNSUPPORTED` | `UNSUPPORTED` |
| **p99 Tail Latency (H2 Stress)** | **4.28 ms** 🏆 | N/A | 45.45 ms | 47.58 ms |
| **Kernel Subsystem** | **`io_uring` multishot** | `epoll` | `epoll` (mio) | `epoll` (mio) |
| **SIMD Acceleration** | **AVX-512 / AVX2 / SSE4.2** | None | Auto-vectorized | Auto-vectorized |
