# Aegon HttpArena Benchmark Results

> **Note on Data Source:**  
> - **Raw Measurements:** Measured on the official HttpArena dedicated benchmark host during the PR [#1491](https://github.com/MDA2AV/HttpArena/pull/1491) run (`commit 449d73d` / `6e974d35`).
> - **Composite Leaderboards & Ranks:** Calculated directly using HttpArena's official leaderboard scoring script ([`scripts/gen_leaderboard_data.py`](https://github.com/MDA2AV/HttpArena/blob/main/scripts/gen_leaderboard_data.py)) across the latest published benchmark datasets.

**Date:** September 23, 2026  
**Environment:** HttpArena Official Dedicated Benchmark Host  
**PR:** [#1491](https://github.com/MDA2AV/HttpArena/pull/1491) (`add-aegon`)  
**Commit:** `449d73d` / `6e974d35`  
**Toolchain:** C++26 (GCC 14), Linux `io_uring`, OpenSSL, `libdeflate`, Glaze  

---

## 1. Executive Summary & Triple Crown Standing

Based on the official HttpArena scoring engine applied to the latest benchmark dataset, Aegon ranks **#1 in HTTP/1.1**, **#1 in HTTP/2**, and **#1 in HTTP/3** across all web frameworks worldwide.

```
╔═════════════════════════════════════════════════════════════════════════════════════════════╗
║                                      THE TRIPLE CROWN                                       ║
╠═════════════════════════════════════════════════════════════════════════════════════════════╣
║  HTTP/1.1 Composite :  Rank #1 / 99  │  7,517.40 / 8,000 pts (94.0%)  │  +1,275.79 over #2  ║
║  HTTP/2   Composite :  Rank #1 / 30  │  3,571.14 / 4,000 pts (89.3%)  │  +1,300.37 over #2  ║
║  HTTP/3   Composite :  Rank #1 / 15  │  2,000.00 / 2,000 pts (100.0%) │  Flawless Perfect   ║
╚═════════════════════════════════════════════════════════════════════════════════════════════╝
```

- **Completeness:** 4/4 (Routing, Middleware, Request, Response) = 1.00x multiplier.
- **Language Standing:** #1 across every single profile among all C++ entries.

---

## 2. Raw Benchmark Metrics from Runner (All 23 Tests)

| Test Profile | Connections | Throughput (RPS) | Latency / CPU per req | CPU Usage | Memory | Notes |
|---|---|---|---|---|---|---|
| `baseline` | 4,096 | **4,229,784 req/s** | — | 6207.7% | 4.2 GiB | Plaintext query summation |
| `pipelined` | 4,096 | **43,043,846 req/s** | — | 6646.2% | 4.2 GiB | 16x pipelined plaintext |
| `limited-conn` | 4,096 | **2,635,465 req/s** | — | 6032.2% | 4.2 GiB | Short-lived (10 req/conn) |
| `json-comp` | 4,096 | **977,983 req/s** | — | 6595.1% | 4.2 GiB | Gzip compressed JSON |
| `json-comp` | 16,384 | **953,996 req/s** | — | 6806.8% | 4.5 GiB | 16K concurrent compression |
| `json-tls` | 4,096 | **1,343,779 req/s** | — | 6431.0% | 4.7 GiB | TLS 1.3 JSON |
| `8gbit` | 512 | **49,389 req/s** | 70.08 µs/req | 316.3% | 4.3 GiB | 8Gbit line-rate binary echo |
| `static-tls` | 1,024 | **533,509 req/s** | — | 6715.7% | 4.5 GiB | 20 static files over TLS |
| `baseline-h2` | 256 | **14,711,948 req/s** | — | 4543.8% | 4.2 GiB | TLS HTTP/2 |
| `baseline-h2` | 1,024 | **14,868,099 req/s** | — | 4565.8% | 4.3 GiB | TLS HTTP/2 (1K conns) |
| `static-h2` | 256 | **520,437 req/s** | — | 5706.6% | 4.4 GiB | Static files over HTTP/2 |
| `static-h2` | 1,024 | **545,021 req/s** | — | 6495.6% | 4.9 GiB | Static files over HTTP/2 |
| `baseline-h2c` | 256 | **15,206,878 req/s** | — | 4008.6% | 4.2 GiB | Cleartext prior-knowledge H2 |
| `baseline-h2c` | 1,024 | **15,319,881 req/s** | — | 4125.9% | 4.2 GiB | Cleartext prior-knowledge H2 |
| `baseline-h2c` | 4,096 | **14,841,545 req/s** | — | 4051.2% | 4.3 GiB | Cleartext prior-knowledge H2 |
| `json-h2c` | 1,024 | **3,662,079 req/s** | — | 6442.8% | 4.4 GiB | Cleartext H2 JSON |
| `json-h2c` | 4,096 | **3,118,627 req/s** | — | 6494.2% | 5.8 GiB | Cleartext H2 JSON (4K conns) |
| `baseline-h3` | 64 | **3,437,273 req/s** | — | 3839.4% | 4.2 GiB | QUIC / UDP port 8443 |
| `static-h3` | 64 | **294,418 req/s** | — | 3979.4% | 4.4 GiB | Static file delivery over QUIC |
| `latency-1m` | 1,024 | **998,341 req/s** | 21.01 µs/req | 2303.6% | 4.1 GiB | Fixed-rate 1M req/s (p99 157 µs) |
| `latency-10k` | 1,024 | **9,982 req/s** | 29.73 µs/req | 28.4% | 4.1 GiB | Fixed-rate 10K req/s (p99 98 µs) |
| `latency-500k-8cpu` | 1,024 | **498,814 req/s** | 15.94 µs/req | 834.6% | 554 MiB | Paced under 8-core cgroup quota |
| `async` | 32,000 | **1,815,229 req/s** | — | 3477.0% | 4.6 GiB | 32K held conns, 10ms timer coro |

---

## 3. Calculated Composite Leaderboards

*(Calculated using HttpArena's `badge_composite()` formula in `scripts/gen_leaderboard_data.py`)*

### HTTP/1.1 Composite Leaderboard (Out of 8,000)

| Rank | Framework | Language | Composite Score | % of Max | Gap to Aegon |
|:---:|---|---|:---:|:---:|:---:|
| 🥇 **#1** | **`aegon`** | **C++** | **7,517.40 / 8,000** | **94.0%** | **Leader** |
| 🥈 **#2** | `genhttp-ioxide` | C# / Rust | 6,241.61 / 8,000 | 78.0% | -1,275.79 (-17.0%) |
| 🥉 **#3** | `actix` | Rust | 6,085.35 / 8,000 | 76.1% | -1,432.05 (-19.1%) |
| **#4** | `fiber-tuned` | Go | 5,569.20 / 8,000 | 69.6% | -1,948.20 (-25.9%) |
| **#5** | `fulmine.js` | JS / C | 5,451.56 / 8,000 | 68.1% | -2,065.84 (-27.5%) |
| **#6** | `simplew-tuned` | C# | 5,365.79 / 8,000 | 67.1% | -2,151.61 (-28.6%) |
| **#7** | `bun` | Zig / JS | 5,350.41 / 8,000 | 66.9% | -2,166.98 (-28.8%) |
| **#8** | `fiber` | Go | 5,214.43 / 8,000 | 65.2% | -2,302.97 (-30.6%) |
| **#9** | `ntex` | Rust | 5,181.82 / 8,000 | 64.8% | -2,335.58 (-31.1%) |
| **#10**| `true-async-server` | C++ | 4,834.07 / 8,000 | 60.4% | -2,683.33 (-35.7%) |

#### Aegon Profile Points in HTTP/1.1:
- `baseline`: **1,000.00 / 1,000**
- `limited-conn`: **1,000.00 / 1,000**
- `json-comp`: **1,000.00 / 1,000**
- `json-tls`: **1,000.00 / 1,000**
- `latency-1m`: **947.53 / 1,000**
- `latency-10k`: **946.06 / 1,000**
- `async`: **859.42 / 1,000**
- `8gbit`: **764.39 / 1,000**
- **Total:** **7,517.40 / 8,000**

---

### HTTP/2 Composite Leaderboard (Out of 4,000)

| Rank | Framework | Language | Composite Score | % of Max | Gap to Aegon |
|:---:|---|---|:---:|:---:|:---:|
| 🥇 **#1** | **`aegon`** | **C++** | **3,571.14 / 4,000** | **89.3%** | **Leader** |
| 🥈 **#2** | `bun` | Zig / JS | 2,270.77 / 4,000 | 56.8% | -1,300.37 (-36.4%) |
| 🥉 **#3** | `genhttp-ioxide` | C# / Rust | 2,073.84 / 4,000 | 51.8% | -1,497.30 (-41.9%) |
| **#4** | `true-async-server` | C++ | 1,944.21 / 4,000 | 48.6% | -1,626.93 (-45.6%) |
| **#5** | `quarkus-jvm` | Java | 1,200.63 / 4,000 | 30.0% | -2,370.51 (-66.4%) |
| **#6** | `wtx` | Rust | 1,062.72 / 4,000 | 26.6% | -2,508.42 (-70.2%) |
| **#7** | `actix` | Rust | 958.08 / 4,000 | 24.0% | -2,613.06 (-73.2%) |
| **#8** | `helidon-tuned` | Java | 931.76 / 4,000 | 23.3% | -2,639.38 (-73.9%) |

#### Aegon Profile Points in HTTP/2:
- `baseline-h2`: **1,000.00 / 1,000**
- `baseline-h2c`: **1,000.00 / 1,000**
- `json-h2c`: **1,000.00 / 1,000**
- `static-h2`: **571.14 / 1,000**
- **Total:** **3,571.14 / 4,000**

---

### HTTP/3 Composite Leaderboard (Out of 2,000)

| Rank | Framework | Language | Composite Score | % of Max | Gap to Aegon |
|:---:|---|---|:---:|:---:|:---:|
| 🥇 **#1** | **`aegon`** | **C++** | **2,000.00 / 2,000** | **100.0%** | **Flawless Leader** |
| 🥈 **#2** | `bun` | Zig / JS | 1,982.94 / 2,000 | 99.1% | -17.06 (-0.9%) |
| 🥉 **#3** | `h2o-mruby` | C / Ruby | 1,742.10 / 2,000 | 87.1% | -257.90 (-12.9%) |
| **#4** | `genhttp-ioxide` | C# / Rust | 1,293.98 / 2,000 | 64.7% | -706.02 (-35.3%) |
| **#5** | `php-fpm` | PHP / C | 936.37 / 2,000 | 46.8% | -1,063.63 (-53.2%) |
| **#6** | `trillium-tuned` | Rust | 894.65 / 2,000 | 44.7% | -1,105.35 (-55.3%) |

#### Aegon Profile Points in HTTP/3:
- `baseline-h3`: **1,000.00 / 1,000**
- `static-h3`: **1,000.00 / 1,000**
- **Total:** **2,000.00 / 2,000**
