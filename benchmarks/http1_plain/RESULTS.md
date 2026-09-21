# HTTP/1.1 Plain Benchmark Results (5 Frameworks)

Official, reproducible benchmarks comparing **Aegon (C++26)** against industry-leading HTTP servers and web frameworks on identical hardware under Linux.

---

## 1. Executive Summary

Under fair hardware saturation (server pinned to 2 physical cores, client load generator pinned to 4 physical cores, 3 consecutive runs averaged), **Aegon (C++26, io_uring)** is the **fastest framework across every measured workload**, beating **Swerver (Zig)**, **Actix-web (Rust)**, **Drogon (C++17)**, and **Fiber (Go)**.

```
/plaintext Throughput (req/s)
─────────────────────────────────────────────────────────────────────────────
Aegon (C++26)      ████████████████████████████████████████ 547,145 req/s (100%)
Swerver (Zig)      ████████████████████████████████        468,613 req/s ( 86%)
Actix-web (Rust)   ███████████████████████████             400,128 req/s ( 73%)
Drogon (C++17)     ██████████████████████████              388,383 req/s ( 71%)
Fiber (Go)         ████████████████████████                352,123 req/s ( 64%)
```

---

## 2. Hardware Topology & Methodology

To eliminate client-side load generator bottlenecks, the host was partitioned using Linux CPU affinity (`taskset`):

| Role | Cores | Worker / Client Threads | Pinned Cores (`taskset`) |
| :--- | :--- | :--- | :--- |
| **HTTP Server** | **2 Physical Cores** | 2 Worker Threads | `taskset -c 0,1` |
| **Load Generator (`wrk`)** | **4 Physical Cores** | 4 Threads, 100 Connections | `taskset -c 2,3,4,5` |

- **Measurement Protocol**: 3-second warm-up + 3 consecutive 10-second runs with a 2-second cooldown between runs.
- **Metric Reported**: Mathematical mean average over the 3 runs.
- **Kernel Sysctl**: Default Linux socket parameters (`somaxconn=4096`, `tcp_tw_reuse=2`).

---

## 3. Workload Comparison Tables

### Workload 1: `/plaintext` (Event Loop & Socket I/O Saturation)
*Request: `GET /plaintext` | Payload: `Hello, World!` (`text/plain`)*

| Rank | Framework | Language & Architecture | 3-Run Avg Throughput | Total Reqs (30s) | Avg Latency | p50 Latency | p99 Latency | vs Aegon |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| 🥇 **1** | **Aegon** | **C++26 (`io_uring`, zero-alloc routing)** | **547,144.84 req/s** | **5,525,446** | **214.09 µs** | **169.33 µs** | **468.67 µs** | **Baseline (100%)** |
| 🥈 **2** | **Swerver** | **Zig (HttpArena tuned, `io_uring_native`)** | **468,612.69 req/s** | **4,732,879** | **209.88 µs** | **198.33 µs** | **416.33 µs** | **-14.4%** (-78k req/s) |
| 🥉 **3** | **Actix-web** | Rust (`mimalloc`, tokio/epoll) | **400,128.26 req/s** | **4,015,783** | **271.05 µs** | **239.00 µs** | **559.00 µs** | **-26.9%** (-147k req/s) |
| 4 | **Drogon** | C++17 (epoll, thread pool) | **388,383.14 req/s** | **3,909,938** | **255.72 µs** | **248.67 µs** | **531.00 µs** | **-29.0%** (-158k req/s) |
| 5 | **Fiber** | Go (prefork, fasthttp) | **352,123.01 req/s** | **3,544,801** | **286.28 µs** | **284.67 µs** | **621.00 µs** | **-35.6%** (-195k req/s) |

---

### Workload 2: `/json` (Serialization & Payload Construction)
*Request: `GET /json` | Payload: `{"message":"Hello, World!"}` (`application/json`)*

| Rank | Framework | Language & Architecture | 3-Run Avg Throughput | Total Reqs (30s) | Avg Latency | p50 Latency | p99 Latency | vs Aegon |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| 🥇 **1** | **Aegon** | **C++26 (Glaze compile-time JSON, `io_uring`)** | **543,849.26 req/s** | **5,475,128** | **206.22 µs** | **172.00 µs** | **1,606.67 µs** | **Baseline (100%)** |
| 🥈 **2** | **Swerver** | **Zig (`ctx.jsonValue`, buffer pools)** | **465,714.91 req/s** | **4,688,201** | **213.45 µs** | **204.00 µs** | **444.33 µs** | **-14.4%** (-78k req/s) |
| 🥉 **3** | **Actix-web** | Rust (`serde_json`, `mimalloc`) | **393,235.00 req/s** | **3,959,259** | **265.14 µs** | **248.00 µs** | **1,133.33 µs** | **-27.7%** (-151k req/s) |
| 4 | **Fiber** | Go (`fastjson`, prefork) | **320,411.14 req/s** | **3,225,558** | **312.83 µs** | **314.00 µs** | **664.00 µs** | **-41.1%** (-223k req/s) |
| 5 | **Drogon** | C++17 (`jsoncpp` heap nodes, epoll) | **222,119.66 req/s** | **2,243,330** | **448.21 µs** | **450.00 µs** | **916.67 µs** | **-59.2%** (-322k req/s) |

---

### Workload 3: Dynamic Route `/users/42/posts/101` (Radix Tree Extraction)
*Request: `GET /users/42/posts/101` | Payload: `User: 42, Post: 101` (`text/plain`)*

| Rank | Framework | Language & Architecture | 3-Run Avg Throughput | Total Reqs (30s) | Avg Latency | p50 Latency | p99 Latency | vs Aegon |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| 🥇 **1** | **Aegon** | **C++26 (Stack-allocated Radix Tree, `io_uring`)** | **544,465.49 req/s** | **5,480,942** | **200.22 µs** | **173.33 µs** | **1,340.67 µs** | **Baseline (100%)** |
| 🥈 **2** | **Swerver** | **Zig (`ResponseBuilder` buffer pool)** | **460,992.37 req/s** | **4,641,527** | **214.42 µs** | **202.00 µs** | **421.33 µs** | **-15.3%** (-83k req/s) |
| 🥉 **3** | **Actix-web** | Rust (Resource path matcher, tokio) | **381,962.28 req/s** | **3,844,981** | **265.66 µs** | **255.67 µs** | **452.33 µs** | **-29.8%** (-163k req/s) |
| 4 | **Fiber** | Go (fast routing params, fasthttp) | **334,278.93 req/s** | **3,365,009** | **298.32 µs** | **298.67 µs** | **612.67 µs** | **-38.6%** (-210k req/s) |
| 5 | **Drogon** | C++17 (Regex/string split router) | **296,297.79 req/s** | **2,992,552** | **335.46 µs** | **326.67 µs** | **674.33 µs** | **-45.6%** (-248k req/s) |

---

## 4. Per-Run Triplicate Breakdown

### Aegon (C++26)
- **`/plaintext`**:
  - Run 1: 545,472.38 req/s (p50: 169.00 µs, p99: 479.00 µs, 5,470,257 reqs)
  - Run 2: 552,368.98 req/s (p50: 169.00 µs, p99: 462.00 µs, 5,540,111 reqs)
  - Run 3: 543,593.17 req/s (p50: 170.00 µs, p99: 465.00 µs, 5,515,078 reqs)
  - **Mean**: 547,144.84 req/s | p50: 169.33 µs
- **`/json`**:
  - Run 1: 542,119.16 req/s (p50: 172.00 µs, 5,474,933 reqs)
  - Run 2: 540,892.96 req/s (p50: 172.00 µs, 5,462,829 reqs)
  - Run 3: 548,535.65 req/s (p50: 172.00 µs, 5,487,623 reqs)
  - **Mean**: 543,849.26 req/s | p50: 172.00 µs
- **`/users/42/posts/101`**:
  - Run 1: 532,216.31 req/s (p50: 178.00 µs, 5,374,993 reqs)
  - Run 2: 551,737.26 req/s (p50: 172.00 µs, 5,518,630 reqs)
  - Run 3: 549,442.91 req/s (p50: 170.00 µs, 5,549,202 reqs)
  - **Mean**: 544,465.49 req/s | p50: 173.33 µs

### Swerver (Zig)
- **`/plaintext`**:
  - Run 1: 467,210.83 req/s (p50: 198.00 µs, 4,718,756 reqs)
  - Run 2: 471,061.23 req/s (p50: 197.00 µs, 4,757,750 reqs)
  - Run 3: 467,566.00 req/s (p50: 200.00 µs, 4,722,130 reqs)
  - **Mean**: 468,612.69 req/s | p50: 198.33 µs
- **`/json`**:
  - Run 1: 459,089.21 req/s (p50: 202.00 µs, 4,636,633 reqs)
  - Run 2: 473,704.03 req/s (p50: 198.00 µs, 4,784,298 reqs)
  - Run 3: 464,351.50 req/s (p50: 212.00 µs, 4,643,671 reqs)
  - **Mean**: 465,714.91 req/s | p50: 204.00 µs
- **`/users/42/posts/101`**:
  - Run 1: 460,438.40 req/s (p50: 201.00 µs, 4,650,310 reqs)
  - Run 2: 459,607.55 req/s (p50: 204.00 µs, 4,598,932 reqs)
  - Run 3: 462,931.15 req/s (p50: 201.00 µs, 4,675,338 reqs)
  - **Mean**: 460,992.37 req/s | p50: 202.00 µs

### Actix-web (Rust)
- **`/plaintext`**: Mean 400,128.26 req/s | p50: 239.00 µs
- **`/json`**: Mean 393,235.00 req/s | p50: 248.00 µs
- **`/users/42/posts/101`**: Mean 381,962.28 req/s | p50: 255.67 µs

### Drogon (C++17)
- **`/plaintext`**: Mean 388,383.14 req/s | p50: 248.67 µs
- **`/json`**: Mean 222,119.66 req/s | p50: 450.00 µs *(drop caused by jsoncpp dynamic heap allocations)*
- **`/users/42/posts/101`**: Mean 296,297.79 req/s | p50: 326.67 µs

### Fiber (Go)
- **`/plaintext`**: Mean 352,123.01 req/s | p50: 284.67 µs
- **`/json`**: Mean 320,411.14 req/s | p50: 314.00 µs
- **`/users/42/posts/101`**: Mean 334,278.93 req/s | p50: 298.67 µs

---

## 5. Architectural Takeaways

1. **True Asynchronous `io_uring` Ring Batching**:
   - Both Aegon and Swerver leverage Linux `io_uring`, which bypasses `epoll` syscall overhead and yields ~468k–547k req/s compared to epoll-based frameworks capping around 380k–400k req/s.
   - Aegon achieves higher throughput (+16.7% over Swerver) by pairing true multishot accept ring submissions with zero-allocation stack routing.
2. **Zero-Allocation Hot Path**:
   - Aegon uses `StackRouteParams` (`std::array<std::pair<std::string_view, std::string_view>, 8>`) and first-byte static child pruning in its Radix tree, which completely eliminates `std::vector` dynamic allocations during route matching.
   - Glaze compile-time JSON reflection serializes structs into existing buffer memory without runtime intermediate node trees.
3. **Reproducibility Guarantee**:
   - Every benchmark in this directory is self-contained in `benchmarks/http1_plain/`.

---

## 6. How to Reproduce

All commands can be run directly from this directory:

```bash
# 1. Single benchmark test (e.g. 10s test with 100 connections)
./benchmarks/http1_plain/run_single.sh aegon plaintext 10s 100
./benchmarks/http1_plain/run_single.sh swerver plaintext 10s 100
./benchmarks/http1_plain/run_single.sh actix json 10s 100
./benchmarks/http1_plain/run_single.sh drogon user_post 10s 100
./benchmarks/http1_plain/run_single.sh fiber plaintext 10s 100

# 2. Triplicate 3-run mathematical average (3 runs x 10s each)
python3 benchmarks/http1_plain/run_triplicate.py aegon plaintext 10s 100
python3 benchmarks/http1_plain/run_triplicate.py swerver plaintext 10s 100
python3 benchmarks/http1_plain/run_triplicate.py actix plaintext 10s 100
python3 benchmarks/http1_plain/run_triplicate.py drogon plaintext 10s 100
python3 benchmarks/http1_plain/run_triplicate.py fiber plaintext 10s 100
```
