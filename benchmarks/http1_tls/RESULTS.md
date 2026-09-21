# HTTP/1.1 TLS Benchmark Results: Aegon vs Industry Leaders

This document contains the official, fully reproducible benchmark results for **HTTP/1.1 over TLS 1.3 (HTTPS)** comparing **Aegon** against **Swerver (Zig)**, **Fiber (Go)**, **Drogon (C++)**, and **Actix-web (Rust)**.

All benchmarks were conducted under strict hardware isolation, 100% reproducible directory separation, and a scientific triplicate-run methodology.

---

## 🔬 Benchmark Methodology & Environment

- **Protocol**: HTTP/1.1 over TLS 1.3 (`https://127.0.0.1:{PORT}{ENDPOINT}`)
- **Certificates**: ECDSA (Prime256v1 / secp256r1) with SAN `localhost` and `127.0.0.1`
- **Methodology**:
  - **Warmup**: 3 seconds warm-up before measurement to eliminate initial JIT/TLS handshake transients.
  - **Measurement**: **3 independent runs × 10 seconds** per workload (30 seconds total measurement time per framework/endpoint).
  - **Cooldown**: 2-second sleep between triplicate runs and 3-second sleep between frameworks.
  - **Single Execution**: Strictly **one server and one benchmark runner** active at any given moment.
- **CPU Partitioning (Physical Core Isolation)**:
  - **Server Process**: Pinned strictly to physical cores `0, 1` via `taskset -c 0,1` (2 worker threads).
  - **Client Load Generator (`wrk`)**: Pinned strictly to physical cores `2, 3, 4, 5` via `taskset -c 2,3,4,5` (4 threads, 100 concurrent keep-alive connections).
  - Guarantees zero CPU starvation, zero thread migration overhead, and zero core contention.

---

## 📊 Summary of Results (3-Run Triplicate Averages)

### 1. Plaintext (`/plaintext` - TLS 1.3)
> High-throughput baseline testing HTTP parser, connection state machine, OpenSSL/Rustls record encryption, and kernel write efficiency.

| Rank | Framework | Language | Architecture / I/O Backend | Requests/sec (3-Run Avg) | Avg Latency | p50 Latency | p99 Latency | Total Reqs (30s) |
|:---:|:---|:---|:---|:---:|:---:|:---:|:---:|:---:|
| 🥇 | **Aegon** | C++26 | Linux-native `io_uring` + OpenSSL | **423,902.09** | **237.74 µs** | **222.00 µs** | **605.00 µs** | **12,761,803** |
| 🥈 | **Swerver** | Zig | `io_uring_native` + Zig TLS | **382,901.10** | 262.49 µs | 243.33 µs | 655.33 µs | 11,526,615 |
| 🥉 | **Actix-web** | Rust | Tokio epoll + `rustls` | **349,875.17** | 385.24 µs | 268.00 µs | 2,750.00 µs | 10,499,916 |
| 4 | **Fiber** | Go | fasthttp prefork + Go crypto/tls | **327,720.40** | 312.37 µs | 299.67 µs | 766.33 µs | 9,929,688 |
| 5 | **Drogon** | C++17 | trantor epoll + OpenSSL | **319,510.95** | 381.96 µs | 302.67 µs | 677.00 µs | 9,649,272 |

---

### 2. JSON Serialization (`/json` - TLS 1.3)
> Dynamic JSON serialization with dynamically generated UUID v4 and high-resolution microsecond timestamp, followed by TLS record framing.

| Rank | Framework | Language | JSON Engine | Requests/sec (3-Run Avg) | Avg Latency | p50 Latency | p99 Latency | Total Reqs (30s) |
|:---:|:---|:---|:---|:---:|:---:|:---:|:---:|:---:|
| 🥇 | **Aegon** | C++26 | Glaze (SIMD / Zero-copy) | **418,027.14** | **246.23 µs** | **224.33 µs** | **867.00 µs** | **12,583,711** |
| 🥈 | **Swerver** | Zig | `std.json` streaming | **386,190.38** | 254.47 µs | 240.33 µs | 515.33 µs | 11,664,352 |
| 🥉 | **Actix-web** | Rust | Serde JSON + `rustls` | **336,283.59** | 378.78 µs | 281.00 µs | 2,690.00 µs | 10,090,724 |
| 4 | **Fiber** | Go | Go standard `encoding/json` | **296,697.13** | 338.23 µs | 335.33 µs | 726.33 µs | 8,931,249 |
| 5 | **Drogon** | C++17 | JsonCpp | **194,054.45** | 594.25 µs | 512.67 µs | 1,140.00 µs | 5,841,651 |

---

### 3. Dynamic Route with Path Parameters (`/users/42/posts/101` - TLS 1.3)
> URL radix tree matching, zero-copy string extraction of 2 path parameters, integer parsing via `std::from_chars`, and JSON serialization under TLS.

| Rank | Framework | Language | Router Architecture | Requests/sec (3-Run Avg) | Avg Latency | p50 Latency | p99 Latency | Total Reqs (30s) |
|:---:|:---|:---|:---|:---:|:---:|:---:|:---:|:---:|
| 🥇 | **Aegon** | C++26 | Zero-alloc Radix Router | **413,632.71** | **244.45 µs** | **226.00 µs** | **779.67 µs** | **12,532,664** |
| 🥈 | **Swerver** | Zig | Swerver Router | **375,552.83** | 264.54 µs | 248.00 µs | 577.67 µs | 11,379,169 |
| 🥉 | **Actix-web** | Rust | Actix Resource Regex/Table + `rustls` | **330,479.97** | 386.19 µs | 286.67 µs | 2,583.33 µs | 9,950,764 |
| 4 | **Fiber** | Go | fasthttp Tree Router | **307,973.21** | 328.76 µs | 321.33 µs | 748.67 µs | 9,301,955 |
| 5 | **Drogon** | C++17 | Drogon Dynamic Regex Router | **252,762.95** | 460.31 µs | 385.00 µs | 819.00 µs | 7,633,268 |

---

## 📈 Comparison: Plain HTTP/1.1 vs HTTP/1.1 TLS Overhead

| Framework | Plain HTTP/1.1 (Req/s) | HTTP/1.1 TLS (Req/s) | TLS Overhead (% Throughput) |
|:---|:---:|:---:|:---:|
| **Aegon** | **547,402** | **423,902** | **-22.6%** |
| **Swerver** | 468,690 | 382,901 | -18.3% |
| **Actix-web** | 400,248 | 349,875 | **-12.6%** (with `rustls`, down from -31.5% with OpenSSL) |
| **Fiber** | 352,243 | 327,720 | -6.9% |
| **Drogon** | 388,432 | 319,511 | -17.7% |

### Key Takeaways:
1. **Aegon is the only framework to sustain over 410,000 req/s across all TLS workloads on just 2 CPU cores**.
2. **Aegon outperforms Swerver (Zig + `io_uring_native`) by +10.7% on plaintext, +8.2% on JSON, and +10.1% on dynamic routes**.
3. **Actix-web + `rustls` Improvement**: Recompiling Actix-web with `rustls` instead of OpenSSL increased throughput by **+27.7%** on plaintext, **+25.4%** on JSON, and **+26.2%** on dynamic routes, advancing Actix-web from 5th place to 3rd place overall.
4. **Aegon retains a strong +21.2% to +25.2% lead over Actix-web (`rustls`)**, delivering **423k vs 350k req/s** on plaintext with significantly lower p50 latency (222 µs vs 268 µs).
5. **Lowest Median Latency**: Aegon maintained the lowest p50 latency across all workloads (~222-226 µs), delivering faster response times under high concurrency than any competing framework.

---

## 🔁 Reproduction Instructions

To independently reproduce these benchmarks:
```bash
# 1. Run a single framework and workload (e.g. Aegon on plaintext for 10s with 100 connections)
./benchmarks/http1_tls/run_single.sh aegon plaintext 10s 100

# 2. Run triplicate evaluation for any framework/workload
python3 benchmarks/http1_tls/run_triplicate.py swerver json 10s 100

# 3. Run the complete automated suite across all frameworks and workloads
python3 benchmarks/http1_tls/run_all_tls.py
```
