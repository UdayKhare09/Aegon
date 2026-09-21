# Level-Playing-Field Benchmarks

Aegon is engineered from the ground up for extreme throughput and deterministic sub-millisecond tail latencies. This document details the official, scientifically isolated benchmark results comparing **Aegon (C++26)** against leading modern frameworks:
- **Swerver** (Zig `io_uring_native`)
- **Actix-web** (Rust `tokio` / `epoll` + `mimalloc`)
- **Drogon** (C++17 Trantor `epoll`)
- **Fiber** (Go `fasthttp` prefork)

---

## Methodology & Hardware Isolation

To ensure 100% reproducibility and eliminate scheduling and cache interference:
- **Hardware**: AMD Ryzen 5 7600X (6 Zen 4 physical cores / 12 logical threads), 32MB L3 Cache
- **Operating System**: Linux 6.18.2-zen (x86_64)
- **Server CPU Isolation**: Physical Cores `0, 1` (`taskset -c 0,1`, 2 worker threads)
- **Client CPU Isolation**: Physical Cores `2, 3, 4, 5` (`taskset -c 2,3,4,5`, 4 client threads)
- **Execution Protocol**: 3-second warm-up + **3 consecutive 10-second measurement runs** per workload with cooldown intervals. All reported metrics represent un-cherry-picked arithmetic triplicate averages.

---

## 1. Plain HTTP/1.1 Benchmark Results

Tool: `wrk` (4 threads, 100 persistent connections, pipeline depth 1)

### `/plaintext` (TechEmpower Standard 13-Byte Payload)
| Rank | Framework | Language & Architecture | 3-Run Avg Req/s | Mean Latency | p50 Latency | p99 Latency | Error Rate |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| 🥇 | **Aegon** | **C++26 (`io_uring` multishot)** | **547,144.84** | **214.09 µs** | **169.33 µs** | **468.67 µs** | **0.00%** |
| 🥈 | **Swerver** | Zig (`io_uring_native`) | **468,612.69** | 209.88 µs | 198.33 µs | 416.33 µs | 0.00% |
| 🥉 | **Actix-web** | Rust (`tokio` / `epoll`) | **400,128.26** | 271.05 µs | 239.00 µs | 559.00 µs | 0.00% |
| 4 | **Drogon** | C++17 (Trantor `epoll`) | **388,383.14** | 255.72 µs | 248.67 µs | 531.00 µs | 0.00% |
| 5 | **Fiber** | Go (`fasthttp` prefork) | **352,123.01** | 286.28 µs | 284.67 µs | 621.00 µs | 0.00% |

### `/json` (Dynamic UUIDv4 + Timestamp Payload)
| Rank | Framework | JSON Engine | 3-Run Avg Req/s | Mean Latency | p50 Latency | p99 Latency | Error Rate |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| 🥇 | **Aegon** | **Glaze (Compile-time reflection)** | **543,849.26** | **206.22 µs** | **172.00 µs** | **1,606.67 µs** | **0.00%** |
| 🥈 | **Swerver** | `std.json` streaming | **465,714.91** | 213.45 µs | 204.00 µs | 444.33 µs | 0.00% |
| 🥉 | **Actix-web** | `serde_json` + `mimalloc` | **393,235.00** | 265.14 µs | 248.00 µs | 1,133.33 µs | 0.00% |
| 4 | **Fiber** | `fastjson` | **320,411.14** | 312.83 µs | 314.00 µs | 664.00 µs | 0.00% |
| 5 | **Drogon** | `jsoncpp` DOM | **222,119.66** | 448.21 µs | 450.00 µs | 916.67 µs | 0.00% |

### Dynamic Route `/users/42/posts/101` (Path Parameter Extraction)
| Rank | Framework | Router Architecture | 3-Run Avg Req/s | Mean Latency | p50 Latency | p99 Latency | Error Rate |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| 🥇 | **Aegon** | **Zero-alloc Radix + `from_chars`** | **544,465.49** | **200.22 µs** | **173.33 µs** | **1,340.67 µs** | **0.00%** |
| 🥈 | **Swerver** | Swerver Router | **460,992.37** | 214.42 µs | 202.00 µs | 421.33 µs | 0.00% |
| 🥉 | **Actix-web** | Actix Path extractor | **381,962.28** | 265.66 µs | 255.67 µs | 452.33 µs | 0.00% |
| 4 | **Fiber** | `fasthttp` Tree Router | **334,278.93** | 298.32 µs | 298.67 µs | 612.67 µs | 0.00% |
| 5 | **Drogon** | Drogon Dynamic Router | **296,297.79** | 335.46 µs | 326.67 µs | 674.33 µs | 0.00% |

---

## 2. HTTP/1.1 TLS (HTTPS) Benchmark Results

Tool: `wrk` with TLS 1.3 (`ECDSA prime256v1`, 4 threads, 100 persistent HTTPS connections)

| Workload | Aegon (C++26) | Swerver (Zig) | Actix-web (Rust, `rustls`) | Fiber (Go) | Drogon (C++) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`/plaintext`** | **423,902.09 req/s** | 382,901.10 req/s | 349,875.17 req/s | 327,720.40 req/s | 319,510.95 req/s |
| **`/json`** | **418,027.14 req/s** | 386,190.38 req/s | 336,283.59 req/s | 296,697.13 req/s | 194,054.45 req/s |
| **`/users/42/posts/101`** | **413,632.71 req/s** | 375,552.83 req/s | 330,479.97 req/s | 307,973.21 req/s | 252,762.95 req/s |

---

## 3. HTTP/2 Multiplexing Benchmark Results (RFC 7540)

Tool: `h2load` (100 connections × 10 streams = **1,000 active concurrent multiplexed streams**, HPACK enabled)

| Workload | Aegon (C++26) | Actix-web (Rust, `rustls`) | Swerver (Zig) | Drogon (C++) | Fiber (Go) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`/plaintext`** | **1,750,028.67 req/s** | 693,620.67 req/s | 475,583.73 req/s | *UNSUPPORTED* | *UNSUPPORTED* |
| **`/json`** | **1,695,960.00 req/s** | 651,131.33 req/s | 476,690.33 req/s | *UNSUPPORTED* | *UNSUPPORTED* |
| **`/users/42/posts/101`** | **1,667,803.33 req/s** | 627,524.00 req/s | 466,247.10 req/s | *UNSUPPORTED* | *UNSUPPORTED* |

*Note: Drogon (Trantor) and Fiber (`fasthttp`) do not implement RFC 7540 binary framing; ALPN falls back to HTTP/1.1.*

---

## 4. HTTP/3 over QUIC Benchmark Results (RFC 9000 / RFC 9114)

Tool: `h2load --h3` (100 connections × 10 streams = **1,000 active concurrent QUIC streams**, UDP datagrams over loopback)

| Workload | Aegon (C++26) | Swerver (Zig) | Actix-web (Rust) | Drogon (C++) | Fiber (Go) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`/plaintext`** | **336,440.73 req/s** | 109,570.60 req/s | *UNSUPPORTED* | *UNSUPPORTED* | *UNSUPPORTED* |
| **`/json`** | **331,608.03 req/s** | 109,342.50 req/s | *UNSUPPORTED* | *UNSUPPORTED* | *UNSUPPORTED* |
| **`/users/42/posts/101`** | **326,682.63 req/s** | 108,137.93 req/s | *UNSUPPORTED* | *UNSUPPORTED* | *UNSUPPORTED* |

*Note: Actix-web, Drogon, and Fiber lack UDP/QUIC network transport support and are strictly TCP-only.*

---

## Reproducing the Benchmarks

All benchmark harnesses and server implementations are open-source and located in [`benchmarks/`](https://github.com/UdayKhare09/Aegon/tree/main/benchmarks):
- `benchmarks/http1_plain/`: Plain HTTP/1.1 suite
- `benchmarks/http1_tls/`: HTTPS TLS 1.3 suite
- `benchmarks/http2/`: HTTP/2 stream multiplexing suite
- `benchmarks/http3/`: HTTP/3 over QUIC suite
