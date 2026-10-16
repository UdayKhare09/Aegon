# HttpArena Official Benchmarks

Aegon is independently evaluated and verified on [HttpArena](https://http-arena.com) — the premier open bare-metal benchmark suite for modern web servers, networking engines, and asynchronous frameworks.

HttpArena executes automated, isolated, multi-iteration workloads across standardized suites on dedicated bare-metal hardware. Aegon ranks **#1 Worldwide Overall** among standard web frameworks across all 4 major protocol suites (HTTP/1.1, HTTP/2, WebSocket, and HTTP/3) combined.

## 📊 Official Protocol Standings

HttpArena tests frameworks under identical conditions on bare metal (**AMD Ryzen Threadripper PRO 3995WX 64-Core / 128-Thread processor** with physical CPU pinning, kernel optimization, and dedicated 10GbE network interfaces).

| Protocol Suite | Official Rank | Score | Peak Bare-Metal Throughput | Live Benchmark Results |
| :--- | :---: | :---: | :---: | :---: |
| **[HTTP/1.1](https://http-arena.com/#type=emerging,experimental,flagship&tuned=0)** | 🥇 **#1** of 84 | **7,227.92** / 8,000 (90.3%) | **27.6M req/s** (Pipelined) <br> **4.0M+ req/s** (Baseline) | [Explore HTTP/1.1 Leaderboard ↗](https://http-arena.com/#type=emerging,experimental,flagship&tuned=0) |
| **[HTTP/2](https://http-arena.com/#scope=h2&type=emerging,experimental,flagship&tuned=0)** | 🥇 **#1** of 28 | **3,243.47** / 4,000 (81.1%) | **14.4M req/s** (`h2c`) <br> **13.4M req/s** (`h2` TLS) | [Explore HTTP/2 Leaderboard ↗](https://http-arena.com/#scope=h2&type=emerging,experimental,flagship&tuned=0) |
| **[WebSocket](https://http-arena.com/#scope=ws&type=emerging,experimental,flagship&tuned=0)** | 🥇 **#1** of 31 | **3,000.00** / 3,000 (**100.0%**) | **63.6M msg/s** (Pipelined Echo) | [Explore WebSocket Leaderboard ↗](https://http-arena.com/#scope=ws&type=emerging,experimental,flagship&tuned=0) |
| **[HTTP/3](https://http-arena.com/#scope=h3&type=emerging,experimental,flagship&tuned=0)** | 🥉 **#3** of 17 | **884.57** / 2,000 (44.2%) | **2.98M req/s** (QUIC / UDP) | [Explore HTTP/3 Leaderboard ↗](https://http-arena.com/#scope=h3&type=emerging,experimental,flagship&tuned=0) |
| **High Concurrency** | — | — | **2.65M req/s** @ 32,000 concurrent held connections | — |

---

## 🎖️ Official HttpArena Achievements & Medals

In HttpArena's automated scoring, medals are awarded to the top three frameworks in each profile and family composite:

### Gold Medals (🥇 Rank #1 Worldwide)
- **HTTP/1.1 Family Composite**: 🥇 **#1 Overall** (Score: 7,564.9 pts)
- **HTTP/2 Family Composite**: 🥇 **#1 Overall** (Score: 3,243.5 pts)
- **WebSocket Family Composite**: 🥇 **#1 Overall** (**3,000.0 / 3,000 pts — 100.0% Perfect Score**)
- **High Concurrency (`Async Delay`)**: 🥇 **#1 Worldwide** (2.65M req/s @ 32,000 concurrent held connections)
- **Connection Baseline (`HTTP/1.1`)**: 🥇 **#1 Worldwide** (4.0M+ req/s @ 4,096 connections)
- **HTTP/2 Baseline (`h2-TLS`)**: 🥇 **#1 Worldwide** (13.4M req/s @ 256 connections)
- **HTTP/2 Baseline Cleartext (`h2c`)**: 🥇 **#1 Worldwide** (14.4M req/s @ 256 connections)
- **HTTP/2 JSON API (`h2c`)**: 🥇 **#1 Worldwide** (@ 1,024 connections)
- **WebSocket Echo (`ws`)**: 🥇 **#1 Worldwide** (@ 512 connections)
- **WebSocket Echo Pipelined**: 🥇 **#1 Worldwide** (63.6M msg/s @ 512 connections)
- **WebSocket Echo Short-Lived**: 🥇 **#1 Worldwide** (@ 512 connections)
- **Workload JSON Comp**: 🥇 **#1 Worldwide** (@ 4,096 connections)

### Silver Medals (🥈 Rank #2 Worldwide)
- **HTTP/3 Family Composite**: 🥈 **#2 Worldwide**
- **HTTP/3 Baseline**: 🥈 **#2 Worldwide** (2.98M req/s @ 64 connections)
- **HTTP/3 Static File Delivery**: 🥈 **#2 Worldwide** (@ 64 connections)
- **Short-Lived Connections**: 🥈 **#2 Worldwide** (@ 4,096 connections)
- **8Gbit Bandwidth Saturation**: 🥈 **#2 Worldwide** (@ 512 connections)
- **Workload JSON over TLS**: 🥈 **#2 Worldwide** (@ 4,096 connections)

### Bronze Medals (🥉 Rank #3 Worldwide)
- **HTTP/2 Static File Delivery**: 🥉 **#3 Worldwide** (@ 256 connections)

---

## 🛡️ Wire Specification & RFC Conformance

Raw performance is only valuable when accompanied by rigorous protocol correctness and resilience against security vectors. Aegon undergoes continuous testing against the web ecosystem's gold-standard conformance suites:

### Autobahn WebSocket Testsuite (RFC 6455)
- **Score: 296 / 296 Passed (100% Clean Sweep)**
- Validates the entire WebSocket specification: frame boundaries, opcode handling, reserved bits, fragmented control frames, UTF-8 code point validation, SWAR / SIMD unmasking correctness, close handshakes, and compression extensions.

### `h2spec` (RFC 9113 / HTTP/2)
- **Score: 145 Passed, 0 Failed (100% Clean)**
- Strict verification of binary frame parsers, multiplexed stream state machines, flow control credit windows, HPACK dynamic table compression, `SETTINGS` negotiations, priority tree handling, and `RST_STREAM` resets.

### `Http11Probe` (RFC 9110 / RFC 9112)
- **Score: 158 / 159 Passed (99.4%)**
- Exhaustively validates HTTP/1.1 message parsing, CRLF normalization, whitespace handling, header semantics, and full immunity against HTTP Request Smuggling (TE.CL, CL.TE, and dual-header ambiguities).

### `h3spec` (RFC 9000 / RFC 9114 / HTTP/3 & QUIC)
- **Score: 47 / 49 Passed (100% Core HTTP/3 & QPACK)**
- Validates QUIC stream multiplexing, datagram encapsulation, HTTP/3 control and data frame state machines, and QPACK dynamic instruction encoding.

---

## ⚡ Cross-Platform Engine Benchmarks (`io_uring`, `epoll`, `IOCP`)

Aegon features a pluggable I/O engine architecture with zero-allocation stack awaiters. Below are comparative benchmarks executed on isolated physical CPU cores (Server on 2 dedicated cores; Client on 4 dedicated cores):

| Protocol Suite | Workload | Linux `io_uring` (Kernel 6.1+) | Linux `epoll` | Windows `IOCP` (MSVC Native) | Windows Profile |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **HTTP/1.1 Plaintext** | `h2load/wrk -t4 -c100 -D10s` | **528,851 req/s** (203 µs) | **454,290 req/s** (212 µs) | **94,431 req/s** (1.04 ms) | WinSock overlapped I/O |
| **HTTP/1.1 TLS 1.3** | `h2load/wrk -t4 -c100 -D10s` | **398,003 req/s** (249 µs) | **350,448 req/s** (272 µs) | **86,655 req/s** (1.14 ms) | 91.8% of plaintext throughput |
| **HTTP/2 Multiplexing** | `h2load -t4 -c100 -m10` | **1,350,621 req/s** (682 µs) | **1,316,487 req/s** (737 µs) | **355,548 req/s** (2.42 ms) | 3.76x multiplexing speedup |
| **HTTP/3 (QUIC)** | `h2load --h3 -t4 -c50 -m10` | **836,720 req/s** (596 µs) | **873,842 req/s** (572 µs) | **283,238 req/s** (1.62 ms) | 0% packet loss (0 / 403k) |

> [!NOTE]
> - **Linux**: Leverages zero-syscall ring submission via `io_uring` and kernel-mapped buffer pools (`PBUF_RING`) for maximum throughput, with `epoll` providing seamless fallback on Docker and WSL2.
> - **Windows**: Native Windows NT IOCP (`GetQueuedCompletionStatusEx`) delivers stable microsecond-range dispatch, zero packet loss under sustained QUIC load, and sub-3.8 ms p99 latencies.

---

## 🔬 Hardware Architecture & Why Aegon Leads

Aegon achieves these results through architectural innovations across kernel, CPU, and memory boundaries:

1. **Linux `io_uring` Multishot Primitives**
   - Zero-syscall request ingest via `IORING_OP_RECV_MULTISHOT` and direct socket registration via `IORING_OP_ACCEPT_DIRECT`.
   - Kernel-managed buffer rings (`io_uring_buf_ring`) provide zero-copy memory directly from NIC socket receive queues.

2. **C++26 Symmetric-Transfer Coroutines**
   - Eliminates coroutine call frame heap allocation on hot paths using symmetric transfer (`std::coroutine_handle<>::resume()`), achieving sub-microsecond latency.

3. **Stepped SIMD Vector Acceleration**
   - 4-tier vector fallback engine (`AVX-512BW/VL` → `AVX2/BMI2` → `SSE4.2` → `Scalar`) for instant case-insensitive header matching, URI parsing, and token extraction.
   - 5-tier SIMD / SWAR hardware unmasker for WebSocket frame payloads.

4. **Zero-Allocation Protocol Engines**
   - Contiguous response vector packing for pipelined HTTP/1.1.
   - Stack-allocated HPACK/QPACK buffer management for HTTP/2 and HTTP/3.

---

## 🔗 Live Leaderboard & Source Code

- **Live Benchmark Results**: [https://http-arena.com](https://http-arena.com)
- **HttpArena Upstream Suite**: [https://github.com/MDA2AV/HttpArena](https://github.com/MDA2AV/HttpArena)
- **Aegon Upstream PR**: [HttpArena PR #1491 (Merged)](https://github.com/MDA2AV/HttpArena/pull/1491)
- **Aegon Source Code**: [https://github.com/UdayKhare09/Aegon](https://github.com/UdayKhare09/Aegon)
