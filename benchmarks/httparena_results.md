# Aegon HttpArena Benchmark Results

> **Note on Data Source:**  
> - **Raw Measurements:** Measured on the official HttpArena dedicated benchmark host during the PR [#1491](https://github.com/MDA2AV/HttpArena/pull/1491) run (`commit a8d75e94` / `790c8b5`).
> - **Composite Leaderboards & Ranks:** Calculated directly using HttpArena's official leaderboard scoring script ([`scripts/gen_leaderboard_data.py`](https://github.com/MDA2AV/HttpArena/blob/main/scripts/gen_leaderboard_data.py)) across all published benchmark datasets.

**Date:** September 24, 2026  
**Environment:** HttpArena Official Dedicated Benchmark Host  
**PR:** [#1491](https://github.com/MDA2AV/HttpArena/pull/1491) (`add-aegon`)  
**Commit:** `a8d75e94` / `790c8b5` (WebSocket Integration + Multi-protocol)  
**Toolchain:** C++26 (GCC 14), Linux `io_uring`, OpenSSL, `libdeflate`, Glaze  

---

## 1. Executive Summary
Based on the official HttpArena scoring engine applied to the latest benchmark dataset, Aegon ranks **#1 in HTTP/1.1**, **#1 in HTTP/2**, **#1 in HTTP/3**, and **#1 in WebSocket** across all web frameworks worldwide.

```
╔══════════════════════════════════════════════════════════════════════════════════════════════╗
║                                    THE QUADRUPLE Win                                         ║
╠══════════════════════════════════════════════════════════════════════════════════════════════╣
║  HTTP/1.1  Composite :  Rank #1 / 100  │  7,400.79 / 8,000 pts (92.5%)  │  +1,216.80 over #2 ║
║  HTTP/2    Composite :  Rank #1 / 33   │  3,230.13 / 4,000 pts (80.8%)  │  +218.83   over #2 ║
║  HTTP/3    Composite :  Rank #1 / 17   │  1,721.76 / 2,000 pts (86.1%)  │  +52.70    over #2 ║
║  WebSocket Composite :  Rank #1 / 29   │  3,000.00 / 3,000 pts (100.0%) │  Flawless Perfect  ║
╚══════════════════════════════════════════════════════════════════════════════════════════════╝
```

- **Completeness:** 4/4 (Routing, Middleware, Request, Response) = 1.00x multiplier.
- **Language Standing:** #1 across every single profile among all C++ entries.
- **WebSocket Standing:** Flawless 100.0% score across all connection tiers and pipelining depths.

---

## 2. Run-over-Run Comparison (Previous vs Latest PR Run)

Comparison between previous PR benchmark run (`commit 832c17cc`, 23 tests) and the latest full benchmark run (`commit a8d75e94`, 31 tests including WebSocket):

| Test Profile | Previous RPS | New RPS | Δ Throughput | Previous p99 | New p99 | Previous CPU | New CPU | Memory | Status / Notes |
|---|---|---|---|---|---|---|---|---|---|
| `baseline` (4096c) | 4,277,881 | 4,231,339 | -1.09% | 1.32 ms | 1.46 ms | 6445.5% | 6184.5% | 4.2 GiB | Plaintext query summation |
| `pipelined` (4096c) | 43,600,880 | 43,291,308 | -0.71% | 2.06 ms | 1.99 ms | 6629.1% | 6640.6% | 4.2 GiB | 16x pipelined plaintext |
| `limited-conn` (4096c) | 2,647,150 | 2,661,047 | **+0.52%** | 1.92 ms | 1.93 ms | 6044.0% | 6078.0% | 4.2 GiB | 10 req/conn short-lived |
| `json-comp` (4096c) | 981,679 | 985,154 | **+0.35%** | 8.86 ms | 8.81 ms | 6717.7% | 6787.7% | 4.2 GiB | Gzip compressed JSON |
| `json-comp` (16384c) | 956,083 | 954,659 | -0.15% | 23.60 ms | 24.20 ms | 6442.4% | 6777.0% | 4.5 GiB | 16K concurrent compression |
| `json-tls` (4096c) | 1,342,139 | 1,334,245 | -0.59% | 62.00 ms | 67.12 ms | 6570.8% | 6589.2% | 4.8 GiB | TLS 1.3 JSON |
| `8gbit` (512c) | 49,364 | 49,360 | -0.01% | 168.0 µs | 170.0 µs | 306.2% | 315.1% | 4.2 GiB | Paced line-rate binary echo |
| `static-tls` (1024c) | 536,131 | 529,259 | -1.28% | 29.28 ms | 36.06 ms | 6785.7% | 6713.5% | 4.5 GiB | 20 static files over TLS |
| `baseline-h2` (256c) | 14,486,023 | 14,543,836 | **+0.40%** | 14.68 ms | 7.29 ms | 4371.7% | 4512.5% | 4.2 GiB | TLS HTTP/2 multiplexing |
| `baseline-h2` (1024c) | 14,797,722 | 14,496,356 | -2.04% | 93.48 ms | 96.67 ms | 4706.9% | 4690.4% | 4.3 GiB | TLS HTTP/2 (1K conns) |
| `static-h2` (256c) | 525,553 | 555,333 | **+5.67%** | 38.02 ms | 43.37 ms | 5979.7% | 6699.8% | 4.4 GiB | Static files over HTTP/2 |
| `static-h2` (1024c) | 562,094 | 487,518 | -13.27% | 98.75 ms | 158.49 ms | 5965.9% | 6502.3% | 5.0 GiB | High-multiplexing static H2 |
| `baseline-h2c` (256c) | 14,893,439 | 14,653,976 | -1.61% | 7.14 ms | 6.88 ms | 3824.6% | 3988.5% | 4.2 GiB | Cleartext H2 |
| `baseline-h2c` (1024c) | 15,788,772 | 15,706,739 | -0.52% | 11.46 ms | 15.29 ms | 4264.9% | 4308.3% | 4.2 GiB | Cleartext H2 (1K conns) |
| `baseline-h2c` (4096c) | 14,888,196 | 14,766,424 | -0.82% | 46.51 ms | 45.60 ms | 4045.5% | 4250.3% | 4.4 GiB | Cleartext H2 (4K conns) |
| `json-h2c` (1024c) | 3,675,222 | 3,680,638 | **+0.15%** | 23.15 ms | 22.21 ms | 6591.2% | 6492.8% | 4.4 GiB | Cleartext H2 JSON |
| `json-h2c` (4096c) | 3,094,589 | 3,109,662 | **+0.49%** | 96.85 ms | 87.74 ms | 6523.6% | 6335.4% | 5.3 GiB | Cleartext H2 JSON (4K conns) |
| `baseline-h3` (64c) | 3,344,869 | 3,342,752 | -0.06% | 3.06 ms | 3.24 ms | 3729.3% | 3763.2% | 4.2 GiB | HTTP/3 over QUIC/UDP |
| `static-h3` (64c) | 351,724 | 304,544 | -13.41% | 37.79 ms | 33.63 ms | 4888.4% | 4315.8% | 4.3 GiB | Static file delivery over QUIC |
| `latency-1m` (1024c) | 997,968 | 997,803 | -0.02% | 159.0 µs | 163.0 µs | 2309.7% | 2350.1% | 4.1 GiB | Fixed-rate 1M req/s paced |
| `latency-10k` (1024c) | 9,982 | 9,983 | **+0.01%** | 97.0 µs | 98.0 µs | 28.6% | 28.7% | 4.1 GiB | Fixed-rate 10K req/s paced |
| `latency-500k-8cpu` (1024c) | 496,457 | 495,918 | -0.11% | 636.7 ms | 780.0 ms | 839.1% | 833.7% | 555 MiB | 8-core cgroup quota |
| `async` (32000c) | 1,816,143 | 1,816,298 | **+0.01%** | 19.90 ms | 19.80 ms | 3468.5% | 3430.1% | 4.6 GiB | 32K conns, 10ms timer coro |
| **`echo-ws`** (512c) | — | **4,102,815** | **NEW ✨** | — | **214 µs** | — | 6428.3% | 4.1 GiB | WebSocket echo throughput |
| **`echo-ws`** (4096c) | — | **4,371,217** | **NEW ✨** | — | **1.25 ms** | — | 6191.1% | 4.2 GiB | WebSocket echo (4K conns) |
| **`echo-ws`** (16384c) | — | **4,039,162** | **NEW ✨** | — | **5.33 ms** | — | 6134.6% | 4.6 GiB | WebSocket echo (16K conns) |
| **`echo-ws-pipeline`** (512c) | — | **62,172,707** | **NEW ✨** | — | **261 µs** | — | 6280.6% | 4.1 GiB | Batched WS (512 conns) |
| **`echo-ws-pipeline`** (4096c) | — | **67,059,395** | **NEW ✨** | — | **1.30 ms** | — | 6441.9% | 4.2 GiB | Batched WS (4K conns) |
| **`echo-ws-pipeline`** (16384c) | — | **61,827,241** | **NEW ✨** | — | **5.19 ms** | — | 6421.7% | 4.6 GiB | Batched WS (16K conns) |
| **`echo-ws-limited`** (512c) | — | **2,171,092** | **NEW ✨** | — | **374 µs** | — | 5526.2% | 4.2 GiB | 10 msgs/conn short-lived WS |
| **`echo-ws-limited`** (4096c) | — | **2,529,153** | **NEW ✨** | — | **1.78 ms** | — | 6098.3% | 4.3 GiB | 10 msgs/conn (4K conns) |

---

## 3. HttpArena Composite Leaderboards

*(Generated using HttpArena's official composite scoring algorithm `gen_leaderboard_data.py` across all 100+ registered frameworks, Not official yet on the site as under review).*

### 3.1 HTTP/1.1 Composite Leaderboard (Out of 8,000 pts)

Scored profiles: `baseline`, `limited-conn`, `async`, `latency-10k`, `latency-1m`, `json-comp`, `json-tls`, `8gbit`.

| Rank | Framework | Language | Mode | Score / 8,000 | % of Max | Gap to Aegon |
|:---:|---|---|:---:|:---:|:---:|:---:|
| 🥇 **#1** | **`aegon`** | **C++** | **standard** | **7,400.79** | **92.5%** | **Leader** |
| 🥈 **#2** | `genhttp-ioxide` | C# | standard | 6,183.99 | 77.3% | -1,216.80 (-16.4%) |
| 🥉 **#3** | `nilo` | Zig | standard | 6,052.66 | 75.7% | -1,348.13 (-18.2%) |
| **#4** | `actix` | Rust | standard | 6,010.91 | 75.1% | -1,389.88 (-18.8%) |
| **#5** | `swerver` | Zig | tuned | 5,921.52 | 74.0% | -1,479.27 (-20.0%) |
| **#6** | `fulmine.js` | JS | standard | 5,574.53 | 69.7% | -1,826.26 (-24.7%) |
| **#7** | `fiber-tuned` | Go | tuned | 5,527.18 | 69.1% | -1,873.61 (-25.3%) |
| **#8** | `simplew-tuned` | C# | tuned | 5,314.53 | 66.4% | -2,086.26 (-28.2%) |
| **#9** | `bun` | TS | standard | 5,175.97 | 64.7% | -2,224.82 (-30.1%) |
| **#10** | `fiber` | Go | standard | 5,145.37 | 64.3% | -2,255.42 (-30.5%) |

---

### 3.2 HTTP/2 Composite Leaderboard (Out of 4,000 pts)

Scored profiles: `baseline-h2`, `static-h2`, `baseline-h2c`, `json-h2c`.

| Rank | Framework | Language | Mode | Score / 4,000 | % of Max | Gap to Aegon |
|:---:|---|---|:---:|:---:|:---:|:---:|
| 🥇 **#1** | **`aegon`** | **C++** | **standard** | **3,230.13** | **80.8%** | **Leader** |
| 🥈 **#2** | `swerver` | Zig | tuned | 3,011.30 | 75.3% | -218.83 (-6.8%) |
| 🥉 **#3** | `bun` | TS | standard | 1,969.60 | 49.2% | -1,260.53 (-39.0%) |
| **#4** | `genhttp-ioxide` | C# | standard | 1,882.05 | 47.1% | -1,348.08 (-41.7%) |
| **#5** | `true-async-server` | PHP | tuned | 1,619.84 | 40.5% | -1,610.30 (-49.9%) |
| **#6** | `quarkus-jvm` | Java | tuned | 1,106.16 | 27.7% | -2,123.98 (-65.8%) |
| **#7** | `wtx` | Rust | standard | 939.33 | 23.5% | -2,290.80 (-70.9%) |
| **#8** | `actix` | Rust | standard | 868.07 | 21.7% | -2,362.07 (-73.1%) |
| **#9** | `helidon-tuned` | Java | tuned | 835.09 | 20.9% | -2,395.05 (-74.1%) |
| **#10** | `effinitive` | C# | standard | 661.72 | 16.5% | -2,568.41 (-79.5%) |

---

### 3.3 HTTP/3 Composite Leaderboard (Out of 2,000 pts)

Scored profiles: `baseline-h3`, `static-h3`.

| Rank | Framework | Language | Mode | Score / 2,000 | % of Max | Gap to Aegon |
|:---:|---|---|:---:|:---:|:---:|:---:|
| 🥇 **#1** | **`aegon`** | **C++** | **standard** | **1,721.76** | **86.1%** | **Leader** |
| 🥈 **#2** | `bun` | TS | standard | 1,669.07 | 83.5% | -52.70 (-3.1%) |
| 🥉 **#3** | `h2o-mruby` | Ruby | tuned | 1,468.28 | 73.4% | -253.49 (-14.7%) |
| **#4** | `swerver` | Zig | tuned | 1,230.89 | 61.5% | -490.88 (-28.5%) |
| **#5** | `genhttp-ioxide` | C# | standard | 1,083.06 | 54.2% | -638.70 (-37.1%) |
| **#6** | `trillium-tuned` | Rust | tuned | 681.95 | 34.1% | -1,039.82 (-60.4%) |
| **#7** | `trillium` | Rust | standard | 662.97 | 33.1% | -1,058.80 (-61.5%) |
| **#8** | `php-fpm` | PHP | standard | 647.26 | 32.4% | -1,074.50 (-62.4%) |
| **#9** | `genhttp-kestrel` | C# | standard | 570.92 | 28.5% | -1,150.85 (-66.8%) |
| **#10** | `fastendpoints` | C# | standard | 532.84 | 26.6% | -1,188.92 (-69.1%) |

---

### 3.4 WebSocket Composite Leaderboard (Out of 3,000 pts)

Scored profiles: `echo-ws`, `echo-ws-pipeline`, `echo-ws-limited`.

| Rank | Framework | Language | Mode | Score / 3,000 | % of Max | Gap to Aegon |
|:---:|---|---|:---:|:---:|:---:|:---:|
| 🥇 **#1** | **`aegon`** | **C++** | **standard** | **3,000.00** | **100.0%** | **Leader (Flawless)** |
| 🥈 **#2** | `genhttp-ioxide` | C# | standard | 2,335.03 | 77.8% | -664.97 (-22.2%) |
| 🥉 **#3** | `nilo` | Zig | standard | 2,321.48 | 77.4% | -678.52 (-22.6%) |
| **#4** | `wtx` | Rust | standard | 2,267.99 | 75.6% | -732.01 (-24.4%) |
| **#5** | `fulmine.js` | JS | standard | 2,228.89 | 74.3% | -771.11 (-25.7%) |
| **#6** | `fiber` | Go | standard | 1,593.81 | 53.1% | -1,406.19 (-46.9%) |
| **#7** | `bun-websocket` | TS | tuned | 1,549.07 | 51.6% | -1,450.93 (-48.4%) |
| **#8** | `genhttp` | C# | standard | 1,515.63 | 50.5% | -1,484.37 (-49.5%) |
| **#9** | `actix` | Rust | standard | 1,450.36 | 48.3% | -1,549.64 (-51.7%) |
| **#10** | `beskar-websocket` | C# | standard | 1,101.15 | 36.7% | -1,898.85 (-63.3%) |

---
