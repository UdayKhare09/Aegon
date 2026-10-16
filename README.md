# Aegon

<p align="center">
  <strong>Cross-Platform C++26 Asynchronous Web Framework & Compile-Time ORM</strong><br>
  <em>Powered by <code>io_uring</code>, <code>epoll</code>, and native Windows <code>IOCP</code>, C++26 symmetric-transfer coroutines, stepped SIMD vectorization, and compile-time reflection-free data engines.</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.1.a2--alpha-blue.svg" alt="Version 0.1.a2" />
  <img src="https://img.shields.io/badge/C%2B%2B-26-orange.svg" alt="C++26" />
  <img src="https://img.shields.io/badge/engines-io__uring%20%7C%20epoll%20%7C%20IOCP-green.svg" alt="I/O Engines" />
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License" />
</p>

<p align="center">
  <a href="https://http-arena.com/#type=emerging,experimental,flagship&tuned=0">
    <img src="https://img.shields.io/endpoint?url=https://http-arena.com/badge/aegon/h1.json" alt="HTTP/1.1 #1" />
  </a>
  <a href="https://http-arena.com/#scope=h2&type=emerging,experimental,flagship&tuned=0">
    <img src="https://img.shields.io/endpoint?url=https://http-arena.com/badge/aegon/h2.json" alt="HTTP/2 #1" />
  </a>
  <a href="https://http-arena.com/#scope=ws&type=emerging,experimental,flagship&tuned=0">
    <img src="https://img.shields.io/endpoint?url=https://http-arena.com/badge/aegon/ws.json" alt="WebSocket #1" />
  </a>
  <a href="https://http-arena.com/#scope=h3&type=emerging,experimental,flagship&tuned=0">
    <img src="https://img.shields.io/endpoint?url=https://http-arena.com/badge/aegon/h3.json" alt="HTTP/3 #2" />
  </a>
</p>

> ### ⚠️ Project Status: Version `0.1.a2` (Early Alpha)
>
> Aegon is pioneering bleeding-edge C++26 language features, kernel `io_uring` multishot pipelines, and hardware-vectorized protocol engines:
> - **Aegon is in active early alpha development (`v0.1.a2`).**
> - **Public APIs, configuration structures, and internal behaviors will and can have breaking changes** in future releases as the framework evolves and matures.
> - Feedback, issue reports, architectural critiques, and pull requests are warmly invited as we shape Aegon towards stability!

---

## ⚡ Highlights

- **Pluggable Cross-Platform I/O Architecture**: Unified polymorphic `IoEngine` interface supporting native Linux 6.1+ `io_uring` multishot pipelines with `PBUF_RING`, automatic high-speed `epoll` fallback for Linux < 6.1, WSL2, and Docker, and native Windows NT `IOCP` via MSVC (delivering 355k H2 req/s and 283k H3 req/s with 0% packet loss).
- **C++26 Symmetric-Transfer Coroutines**: Pure `aegon::core::Task<T>` async programming with zero heap frame allocations on hot paths.
- **Hardware-Aware Stepped SIMD**: 4-tier vector fallback engine (`AVX-512BW/VL` → `AVX2/BMI2` → `SSE4.2` → `Scalar`) accelerating case-insensitive header matching, URL decoding, and token discovery.
- **Pure Native Protocols (HTTP/1.1, HTTP/2, HTTP/3 & WebSocket)**:
  - Full **HTTP/1.1** pipelining with batched contiguous response streaming.
  - Full **HTTP/2** multiplexing (`h2c` and `h2-TLS`) with user-space batched frame aggregation and zero-alloc stack header packing.
  - Full **HTTP/3 over QUIC** (RFC 9000 / RFC 9114) via `ngtcp2` + `nghttp3` across all TLS listener ports with dual-stack `SO_REUSEPORT` UDP sockets and progressive `Alt-Svc` header emission.
  - Full **RFC 6455 WebSocket** engine with 5-tier SIMD/SWAR hardware unmasking (`AVX2` → `SSE2` / `ARM NEON` → `SWAR 64-bit` → `32-bit` → `bitwise tail`), zero-copy frame parsing, and backpressure-resilient streaming.
- **Built-In High-Speed Static File Serving**: First-class `router.static_files(...)` supporting precompressed (`.br`, `.gz`) sidecars and nanosecond-precision disk-following in-memory caching (`mtime` revalidation).
- **Static Compile-Time ORM**: Type-safe SQL query builder, migrations, optimistic concurrency control (OCC), and transactional look-aside caching.
- **Native Async Redis**: High-throughput Redis client built directly onto the `io_uring` ring buffer supporting standalone, Sentinel, and Redis Cluster.

---

## 📊 Performance & Conformance Benchmarks

Aegon is independently benchmarked and tracked on [HttpArena](https://http-arena.com) — the open bare-metal benchmark suite evaluating web frameworks under identical conditions on dedicated hardware (**AMD Ryzen Threadripper PRO 3995WX 64-Core bare metal**).

Aegon ranks **#1 Worldwide Overall** among standard web frameworks across all 4 major protocol families (HTTP/1.1, HTTP/2, WebSocket, and HTTP/3) combined:

### 🏆 [HttpArena](https://http-arena.com/#type=emerging,experimental,flagship&tuned=0) Official Standings

| Protocol Suite | Official Standing | Score | Peak Bare-Metal Throughput | Live Benchmark Results |
| :--- | :---: | :---: | :---: | :---: |
| **[HTTP/1.1](https://http-arena.com/#type=emerging,experimental,flagship&tuned=0)** | 🥇 **#1** of 84 | **7,227.92** / 8,000 (90.3%) | **27.6M req/s** (Pipelined) <br> **4.0M+ req/s** (Baseline) | [View on HttpArena ↗](https://http-arena.com/#type=emerging,experimental,flagship&tuned=0) |
| **[HTTP/2](https://http-arena.com/#scope=h2&type=emerging,experimental,flagship&tuned=0)** | 🥇 **#1** of 28 | **3,243.47** / 4,000 (81.1%) | **14.4M req/s** (`h2c`) <br> **13.4M req/s** (`h2` TLS) | [View on HttpArena ↗](https://http-arena.com/#scope=h2&type=emerging,experimental,flagship&tuned=0) |
| **[WebSocket](https://http-arena.com/#scope=ws&type=emerging,experimental,flagship&tuned=0)** | 🥇 **#1** of 31 | **3,000.00** / 3,000 (**100.0%**) | **63.6M msg/s** (Pipelined Echo) | [View on HttpArena ↗](https://http-arena.com/#scope=ws&type=emerging,experimental,flagship&tuned=0) |
| **[HTTP/3](https://http-arena.com/#scope=h3&type=emerging,experimental,flagship&tuned=0)** | 🥉 **#3** of 17 | **884.57** / 2,000 (44.2%) | **2.98M req/s** (QUIC / UDP) | [View on HttpArena ↗](https://http-arena.com/#scope=h3&type=emerging,experimental,flagship&tuned=0) |
| **High Concurrency** | — | — | **2.65M req/s** @ 32,000 concurrent held connections | — |

---

### ⚡ Cross-Platform Engine Benchmarks (2 Dedicated Cores)

Aegon provides a zero-allocation pluggable engine abstraction (`IoEngine`) ensuring identical application code runs natively across platforms:

| Protocol Suite | Workload | Linux `io_uring` (Kernel 6.1+) | Linux `epoll` | Windows `IOCP` (MSVC Native) |
| :--- | :--- | :--- | :--- | :--- |
| **HTTP/1.1 Plaintext** | `h2load/wrk -t4 -c100 -D10s` | **528,851 req/s** (203 µs) | **454,290 req/s** (212 µs) | **94,431 req/s** (1.04 ms) |
| **HTTP/1.1 TLS 1.3** | `h2load/wrk -t4 -c100 -D10s` | **398,003 req/s** (249 µs) | **350,448 req/s** (272 µs) | **86,655 req/s** (1.14 ms) |
| **HTTP/2 Multiplexing** | `h2load -t4 -c100 -m10` | **1,350,621 req/s** (682 µs) | **1,316,487 req/s** (737 µs) | **355,548 req/s** (2.42 ms) |
| **HTTP/3 (QUIC)** | `h2load --h3 -t4 -c50 -m10` | **836,720 req/s** (596 µs) | **873,842 req/s** (572 µs) | **283,238 req/s** (1.62 ms, 0% loss) |
| **Test Suite Pass Rate**| `ctest` | **46 / 46 (100%)** | **46 / 46 (100%)** | **41 / 41 (100%)** |

---

### 🛡️ Protocol Specification & RFC Conformance

Raw throughput is meaningless if the server violates wire protocol specifications or is vulnerable to smuggling and framing exploits. Aegon is strictly verified against the industry's most rigorous conformance and fuzzing suites while maintaining peak zero-syscall performance:

- **Autobahn WebSocket Testsuite (RFC 6455)**: **296 / 296 Passed (100% Clean Sweep)** — Validated against all framing edge cases, fragmented payloads, UTF-8 code point boundaries, mask permutations, and compression extensions.
- **`h2spec` (RFC 9113 / HTTP/2)**: **145 Passed, 0 Failed (100% Clean)** — Complete compliance across concurrent streams, frame state machines, flow control windows, `RST_STREAM`, and priority tree handling.
- **`Http11Probe` (RFC 9110 / RFC 9112)**: **158 / 159 Passed (99.4%)** — Fully hardened against HTTP request smuggling (TE.CL / CL.TE), invalid transfer-encoding permutations, and header injection vectors.
- **`h3spec` (RFC 9000 / RFC 9114 / HTTP/3 & QUIC)**: **47 / 49 Passed (100% Core HTTP/3 & QPACK)** — Wire-level conformance across frame encoders, pseudo-headers, stream lifecycles, and dynamic QPACK instruction tables.

> 📖 **Reproducing Benchmarks**: Detailed reproduction instructions, latency percentiles, and testsuite harnesses are available in [`benchmarks/`](benchmarks/) and the [Benchmark Guide](docs/guide/benchmarks.md).

---

## 🚀 Quick Start

### 1. Fast Track via `aegon` CLI

The fastest way to build with Aegon is using the developer CLI:

```bash
# Verify kernel and hardware acceleration capabilities
aegon doctor

# Create a modern C++26 application with layered YAML and .env config
aegon new my_service

# Build and run with hot execution
cd my_service
aegon run
```

### 2. Modern CMake Integration (`find_package`)

Install Aegon system-wide (e.g. building from source with `sudo cmake --install build`):

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Aegon REQUIRED)

add_executable(my_service src/main.cpp)

# Link strictly modular component targets
target_link_libraries(my_service PRIVATE
    Aegon::http
    Aegon::core
    Aegon::config
    Aegon::orm     # Unified SQL ORM (SQLite3 & PostgreSQL)
    Aegon::redis   # Native async Redis
)
```

### 3. Asynchronous Server (`src/main.cpp`)

```cpp
#include <aegon/http/Server.h>
#include <aegon/core/Task.h>
#include <iostream>

using namespace aegon::http;

int main() {
    Server server;

    // Asynchronous coroutine route (co_await ready)
    server.router().get("/user/:id", [](Context& ctx) -> aegon::core::Task<void> {
        auto user_id = ctx.req().param("id").value_or("unknown");
        ctx.res().json({{"id", std::string(user_id)}, {"status", "active"}});
        co_return;
    });

    // Zero-overhead synchronous route
    server.router().get("/health", [](Context& ctx) {
        ctx.res().text("OK");
    });

    // Start server on port 8080 with 6 worker threads
    server.listen(8080).run(6);
    return 0;
}
```

### Enabling TLS and HTTP/3 over QUIC

```cpp
Server server;
server.listen(8443);
server.enable_tls("/path/to/cert.pem", "/path/to/key.pem");
server.enable_http3(true); // Spins up dual-stack QUIC listener on UDP 8443
server.run(6);
```

---

## 📚 Documentation

The full documentation site is built with VitePress and available under `docs/`:

```bash
# Run documentation locally
npm install
npm run docs:dev
```

Topics covered:
- [Getting Started & Installation](docs/guide/getting-started.md)
- [CLI Tooling (`aegon`)](docs/guide/cli.md)
- [Routing, Param Matching & Groups](docs/guide/routing.md)
- [Layered Configuration (`config.yml` & `.env`)](docs/guide/config.md)
- [C++26 Tasks & io_uring Kernel Engine](docs/guide/core/task.md)
- [Type-Safe SQL Engine & Static ORM](docs/guide/data/sql/schema.md)
- [Native Async Redis Client](docs/guide/data/redis/client.md)

---

## 📄 License

Aegon is licensed under the [MIT License](LICENSE).
