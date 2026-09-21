# HTTP/2 Benchmark Results: Aegon vs Industry Leaders

This document contains the official, fully reproducible benchmark results for **RFC 7540 HTTP/2 over TLS 1.3 (HTTPS)** comparing **Aegon** against **Swerver (Zig)**, **Actix-web (Rust)**, **Drogon (C++)**, and **Fiber (Go)**.

All benchmarks were conducted using **`h2load`** (nghttp2 v1.70.0) with strict hardware core isolation, full stream multiplexing, persistent keep-alive connections, HPACK compression, and scientific triplicate runs.

---

## 🔬 Benchmark Methodology & Environment

- **Protocol**: HTTP/2 over TLS 1.3 (`https://127.0.0.1:{PORT}{ENDPOINT}`) with ALPN `h2`
- **Tool**: `/usr/bin/h2load` (nghttp2 v1.70.0)
- **Multiplexing Configuration**:
  - **Connections (`-c`)**: 100 persistent keep-alive connections
  - **Concurrent Streams (`-m`)**: 10 concurrent streams per connection
  - **Total In-Flight Multiplexed Streams**: **1,000 active concurrent streams**
- **Methodology**:
  - **Warmup**: 3 seconds warm-up per run to prime TLS sessions, HPACK dynamic tables, and stream pipelines.
  - **Measurement**: **3 independent runs × 10 seconds** per workload (30 seconds total measurement time per framework/endpoint).
  - **Cooldown**: 2-second sleep between triplicate runs and 3-second sleep between frameworks.
  - **Single Execution**: Strictly **one server and one benchmark runner** active at any given moment.
- **CPU Partitioning (Physical Core Isolation)**:
  - **Server Process**: Pinned strictly to physical cores `0, 1` via `taskset -c 0,1` (2 worker threads/processes).
  - **Client Load Generator (`h2load`)**: Pinned strictly to physical cores `2, 3, 4, 5` via `taskset -c 2,3,4,5` (4 threads, 100 connections, 1,000 concurrent streams).

---

## 📊 Summary of Results (3-Run Triplicate Averages)

### 1. Plaintext (`/plaintext` - HTTP/2 Multiplexing)
> High-throughput baseline testing HTTP/2 binary frame packing, stream multiplexing, HPACK header compression, and `io_uring` multishot write batching.

| Rank | Framework | Language | Architecture / Engine | Requests/sec (3-Run Avg) | Mean Latency | p50 Latency | p99 Latency | Total Reqs (30s) | Status |
|:---:|:---|:---|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| 🥇 | **Aegon** | C++26 | Linux-native `io_uring` + `nghttp2` | **1,778,203.67** | **515.00 µs** | **512.00 µs** | **716.67 µs** | **17,782,037** | **SUPPORTED** |
| 🥈 | **Actix-web** | Rust | Tokio epoll + `h2` crate (`rustls`) | **693,127.00** | 1,443.33 µs | 1,406.67 µs | 1,716.67 µs | 6,931,270 | **SUPPORTED** |
| 🥉 | **Swerver** | Zig | `io_uring_native` + Swerver HTTP/2 | **460,517.57** | 2,140.00 µs | 2,143.33 µs | 4,013.33 µs | 4,605,176 | **SUPPORTED** |
| 4 | **Drogon** | C++17 | Trantor epoll | *N/A* | - | - | - | - | `UNSUPPORTED`* |
| 5 | **Fiber** | Go | fasthttp prefork | *N/A* | - | - | - | - | `UNSUPPORTED`* |

*\*Note: Neither Drogon (Trantor) nor Fiber (`fasthttp`) implements RFC 7540 HTTP/2 binary framing; ALPN falls back to HTTP/1.1.*

---

### 2. JSON Serialization (`/json` - HTTP/2 Multiplexing)
> Dynamic JSON payload (UUID v4 + timestamp) serialized and framed into HTTP/2 `DATA` frames under 1,000 active concurrent multiplexed streams.

| Rank | Framework | Language | JSON Engine | Requests/sec (3-Run Avg) | Mean Latency | p50 Latency | p99 Latency | Total Reqs (30s) | Status |
|:---:|:---|:---|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| 🥇 | **Aegon** | C++26 | Glaze (Zero-copy reflection) | **1,678,171.33** | **551.00 µs** | **546.00 µs** | **781.00 µs** | **16,781,713** | **SUPPORTED** |
| 🥈 | **Actix-web** | Rust | Serde JSON + `h2` (`rustls`) | **640,796.00** | 1,563.33 µs | 1,513.33 µs | 2,260.00 µs | 6,407,960 | **SUPPORTED** |
| 🥉 | **Swerver** | Zig | `std.json` streaming + Swerver HTTP/2 | **467,039.00** | 2,113.33 µs | 2,153.33 µs | 4,056.67 µs | 4,670,390 | **SUPPORTED** |
| 4 | **Drogon** | C++17 | JsonCpp DOM | *N/A* | - | - | - | - | `UNSUPPORTED` |
| 5 | **Fiber** | Go | Go `encoding/json` | *N/A* | - | - | - | - | `UNSUPPORTED` |

---

### 3. Dynamic Route with Path Parameters (`/users/42/posts/101` - HTTP/2 Multiplexing)
> Radix tree parameter extraction (`:id`, `:post_id`), integer conversion, and response emission over interleaved HTTP/2 stream multiplexing.

| Rank | Framework | Language | Router Architecture | Requests/sec (3-Run Avg) | Mean Latency | p50 Latency | p99 Latency | Total Reqs (30s) | Status |
|:---:|:---|:---|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| 🥇 | **Aegon** | C++26 | Zero-alloc Radix Router | **1,643,887.00** | **565.00 µs** | **559.67 µs** | **762.67 µs** | **16,438,870** | **SUPPORTED** |
| 🥈 | **Actix-web** | Rust | Actix Resource Table (`rustls`) | **616,159.33** | 1,623.33 µs | 1,586.67 µs | 1,973.33 µs | 6,161,593 | **SUPPORTED** |
| 🥉 | **Swerver** | Zig | Swerver Router | **460,464.87** | 2,146.67 µs | 2,166.67 µs | 3,476.67 µs | 4,604,649 | **SUPPORTED** |
| 4 | **Drogon** | C++17 | Drogon Dynamic Router | *N/A* | - | - | - | - | `UNSUPPORTED` |
| 5 | **Fiber** | Go | fasthttp Tree Router | *N/A* | - | - | - | - | `UNSUPPORTED` |

---

## 🏆 Architectural Analysis & Key Insights

1. **Massive Throughput Superiority**:
   - Aegon achieves **1.77 Million req/s** on 2 CPU cores, outperforming Actix-web (`rustls`) by **+156.5% (2.56× faster)** and Swerver by **+286% (3.86× faster)**.
   - Across the 3 workloads (90 seconds of total measurement), Aegon delivered **51,002,620 requests (over 51 MILLION)** with **0 failed requests, 0 errored, and 0 timeouts**.
2. **Sub-Millisecond Median and Tail Latencies**:
   - Under 1,000 active concurrent multiplexed streams, Aegon maintained a **median latency of ~512–559 µs** and a **p99 tail latency under 782 µs**.
   - Competing engines experienced 3× to 4× higher latencies under identical stream multiplexing load: Actix-web p50 was ~1,406–1,586 µs; Swerver p50 was ~2,143–2,166 µs.
3. **Actix-web + `rustls` Improvement**:
   - Rebuilding Actix-web with `rustls` increased HTTP/2 throughput by **+11.8%** on plaintext (619k → 693k req/s), **+12.7%** on JSON (568k → 640k req/s), and **+11.9%** on dynamic routes (550k → 616k req/s).
   - **Zero-Sycall `io_uring` Multishot Loop**: While Tokio (`epoll`) and traditional event loops incur repeated `epoll_ctl` and `writev` syscalls per stream response, Aegon batches nghttp2 frame output directly into registered `io_uring` ring buffers.
   - **Zero-Allocation Stack Header Packing**: HTTP/2 response headers (`:status`, `content-type`, `content-length`) are emitted directly on the stack via `std::array<nghttp2_nv, 16>`, avoiding dynamic vector allocations per stream dispatch.
   - **Glaze SIMD JSON Serialization**: Serializes structs directly into outgoing stream memory buffers without DOM tree generation or intermediate allocations.

---

## 🔁 Reproduction Instructions

To reproduce these benchmarks:
```bash
# 1. Run a single framework and workload (e.g. Aegon on plaintext for 10s with 100 conns and 10 streams/conn)
./benchmarks/http2/run_single.sh aegon plaintext 10s 100 10

# 2. Run triplicate evaluation for any framework/workload
python3 benchmarks/http2/run_triplicate.py swerver json 10s 100 10

# 3. Run the complete automated suite across all workloads
python3 benchmarks/http2/run_all_http2.py
```
