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
| 🥈 | **Swerver** | Zig (`io_uring_native`) | **468,443.08** | 250.77 µs | 186.67 µs | 647.67 µs | 0.00% |
| 🥉 | **Actix-web** | Rust (`tokio` / `epoll`) | **400,242.06** | 260.67 µs | 180.33 µs | 741.33 µs | 0.00% |
| 4 | **Drogon** | C++17 (Trantor `epoll`) | **388,416.48** | 267.08 µs | 196.67 µs | 703.67 µs | 0.00% |
| 5 | **Fiber** | Go (`fasthttp` prefork) | **352,504.60** | 283.67 µs | 197.67 µs | 1,023.33 µs | 0.00% |

### `/json` (Dynamic UUIDv4 + Timestamp Payload)
| Rank | Framework | JSON Engine | 3-Run Avg Req/s | Mean Latency | p50 Latency | p99 Latency | Error Rate |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| 🥇 | **Aegon** | **Glaze (Compile-time reflection)** | **540,683.74** | **216.59 µs** | **172.00 µs** | **470.67 µs** | **0.00%** |
| 🥈 | **Swerver** | `std.json` streaming | **463,770.83** | 253.25 µs | 190.67 µs | 648.33 µs | 0.00% |
| 🥉 | **Actix-web** | `serde_json` + `mimalloc` | **396,552.12** | 262.99 µs | 184.00 µs | 747.00 µs | 0.00% |
| 4 | **Drogon** | `jsoncpp` DOM | **378,574.62** | 273.98 µs | 205.33 µs | 722.33 µs | 0.00% |
| 5 | **Fiber** | `encoding/json` | **346,676.10** | 288.42 µs | 203.00 µs | 1,046.67 µs | 0.00% |

### Dynamic Route `/users/42/posts/101` (Path Parameter Extraction)
| Rank | Framework | Router Architecture | 3-Run Avg Req/s | Mean Latency | p50 Latency | p99 Latency | Error Rate |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| 🥇 | **Aegon** | **Zero-alloc Radix + `from_chars`** | **536,260.67** | **218.42 µs** | **173.67 µs** | **476.33 µs** | **0.00%** |
| 🥈 | **Swerver** | Swerver Router | **460,948.72** | 254.79 µs | 193.33 µs | 652.00 µs | 0.00% |
| 🥉 | **Actix-web** | Actix Path extractor | **392,492.20** | 265.73 µs | 187.33 µs | 751.67 µs | 0.00% |
| 4 | **Drogon** | Drogon Dynamic Router | **374,213.90** | 277.17 µs | 208.67 µs | 729.00 µs | 0.00% |
| 5 | **Fiber** | `fasthttp` Tree Router | **341,894.40** | 292.48 µs | 207.67 µs | 1,060.00 µs | 0.00% |

---

## 2. HTTP/1.1 TLS (HTTPS) Benchmark Results

Tool: `wrk` with TLS 1.3 (`ECDSA prime256v1`, 4 threads, 100 persistent HTTPS connections)

| Workload | Aegon (C++26) | Swerver (Zig) | Actix-web (Rust) | Drogon (C++) | Fiber (Go) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`/plaintext`** | **407,247.38 req/s** | 391,373.18 req/s | 338,810.45 req/s | 323,371.12 req/s | 308,016.74 req/s |
| **`/json`** | **403,912.60 req/s** | 388,410.50 req/s | 334,620.10 req/s | 319,842.30 req/s | 304,180.20 req/s |
| **`/users/42/posts/101`** | **401,180.25 req/s** | 385,290.40 req/s | 331,450.80 req/s | 316,500.10 req/s | 301,230.50 req/s |

---

## 3. HTTP/2 Multiplexing Benchmark Results (RFC 7540)

Tool: `h2load` (100 connections × 10 streams = **1,000 active concurrent multiplexed streams**, HPACK enabled)

| Workload | Aegon (C++26) | Swerver (Zig) | Actix-web (Rust) | Drogon (C++) | Fiber (Go) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`/plaintext`** | **1,733,501.00 req/s** | 460,517.57 req/s | 619,958.00 req/s | *UNSUPPORTED* | *UNSUPPORTED* |
| **`/json`** | **1,655,920.33 req/s** | 467,039.00 req/s | 568,478.00 req/s | *UNSUPPORTED* | *UNSUPPORTED* |
| **`/users/42/posts/101`** | **1,636,177.67 req/s** | 460,464.87 req/s | 550,679.67 req/s | *UNSUPPORTED* | *UNSUPPORTED* |

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
