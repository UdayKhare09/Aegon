# Aegon

<p align="center">
  <strong>Linux-Native C++26 Asynchronous Web Framework & Compile-Time ORM</strong><br>
  <em>Powered by <code>io_uring</code> multishot primitives, C++26 symmetric-transfer coroutines, stepped SIMD vectorization, and compile-time reflection-free data engines.</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.1.a2--alpha-blue.svg" alt="Version 0.1.a2" />
  <img src="https://img.shields.io/badge/C%2B%2B-26-orange.svg" alt="C++26" />
  <img src="https://img.shields.io/badge/kernel-io__uring-green.svg" alt="io_uring" />
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License" />
</p>

> ### ⚠️ Project Status: Version `0.1.a2` (Early Alpha)
>
> **Crafted by Human Vision, Synthesized with AI Assistance**
>
> Aegon is an ambitious, high-performance web framework **conceived, designed, and guided by human engineering, with the majority of the implementation code written in deep collaboration with advanced AI systems**.
>
> Because we are actively pioneering bleeding-edge C++26 language features, kernel `io_uring` multishot pipelines, and hardware-vectorized protocol engines:
> - **Aegon is in active early alpha development (`v0.1.a2`)**.
> - **Public APIs, configuration structures, and internal behaviors will and can have breaking changes** in future releases as the framework evolves and matures.
> - Feedback, issue reports, architectural critiques, and pull requests are warmly invited as we shape Aegon towards stability!

---

## ⚡ Highlights

- **Linux-Native `io_uring` Engine**: Complete kernel bypass with zero-syscall request loops via `IORING_OP_RECV_MULTISHOT`, `IORING_OP_ACCEPT_DIRECT`, and kernel-managed buffer rings (`io_uring_buf_ring`).
- **C++26 Symmetric-Transfer Coroutines**: Pure `aegon::core::Task<T>` async programming with zero heap frame allocations on hot paths.
- **Hardware-Aware Stepped SIMD**: 4-tier vector fallback engine (`AVX-512BW/VL` → `AVX2/BMI2` → `SSE4.2` → `Scalar`) accelerating case-insensitive header matching, URL decoding, and token discovery.
- **Pure Native HTTP/1.1, HTTP/2, and HTTP/3**:
  - Full **HTTP/1.1** pipelining with batched contiguous response streaming.
  - Full **HTTP/2** multiplexing (`h2c` and `h2-TLS`) with user-space batched frame aggregation and zero-alloc stack header packing.
  - Full **HTTP/3 over QUIC** (RFC 9000 / RFC 9114) via `ngtcp2` + `nghttp3` on dual-stack `SO_REUSEPORT` UDP sockets.
- **Static Compile-Time ORM**: Type-safe SQL query builder, migrations, optimistic concurrency control (OCC), and transactional look-aside caching.
- **Native Async Redis**: High-throughput Redis client built directly onto the `io_uring` ring buffer supporting standalone, Sentinel, and Redis Cluster.

---

## 🏆 Benchmark Summary

Aegon was systematically evaluated against three industry-leading, production web servers on identical bare-metal hardware (**AMD Ryzen 5 7600X, 6 Zen 4 Cores, AVX-512, Linux 6.13**).

The test workload is a standard `GET /health` endpoint returning `200 OK` (`text/plain`, `"OK"`).

### Protocol Matrix Peak Performance (6 Physical Cores)

| Protocol / Generation | Aegon (C++26) | Drogon (C++17) | Actix-web (Rust) | Axum (Rust) | Aegon Advantage |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **HTTP/1.1 Cleartext (Peak RPS)** | **912,374 req/s** 🏆 | 836,580 req/s | 854,544 req/s | 824,885 req/s | **+6.8% over #2** |
| **HTTP/1.1 TLS (Peak RPS)** | **886,920 req/s** 🏆 | 798,410 req/s | 812,450 req/s | 785,120 req/s | **+9.2% over #2** |
| **HTTP/2 Cleartext (`h2c`)** | **3,596,346 req/s** 🏆 | `UNSUPPORTED` | `UNSUPPORTED` | 605,585 req/s | **6.27x faster** |
| **HTTP/2 TLS (`h2`)** | **3,480,864 req/s** 🏆 | `UNSUPPORTED` | 1,063,276 req/s | 707,069 req/s | **3.27x faster** |
| **HTTP/3 over QUIC (`h3`)** | **183,574 req/s** 🏆 | `UNSUPPORTED` | `UNSUPPORTED` | `UNSUPPORTED` | **Only Framework** |
| **p99 Tail Latency (H2 Stress)** | **4.28 ms** 🏆 | N/A | 45.45 ms | 47.58 ms | **10.6x lower tail** |
| **Kernel Subsystem** | **`io_uring` multishot** | `epoll` | `epoll` (mio) | `epoll` (mio) | Zero syscall overhead |
| **Vector Engine** | **AVX-512 / AVX2 / SSE4.2** | None | Auto-vectorized | Auto-vectorized | Stepped SIMD fallback |

> ⚠️ **Benchmark Disclaimers & Notes**:
> - **Protocol Support**: Drogon does not implement HTTP/2 or HTTP/3 server protocols. Actix-web does not support cleartext `h2c` and lacks official HTTP/3 support. Axum lacks official production HTTP/3 over QUIC support. Marking a framework as `UNSUPPORTED` reflects actual protocol availability in release builds.
> - **Workload Scope**: Minimal endpoint testing measures raw protocol engine throughput, memory allocation overhead, and kernel networking latency. Application-level database queries or business logic frequently become the real-world throughput ceiling.
> - **Hardware**: Benchmarked on bare-metal Zen 4 (Ryzen 5 7600X, 6 physical cores @ up to 5.3GHz, 26GB DDR5, AVX-512 enabled). Results in virtualized hypervisors may vary based on `io_uring` support.
> - See the full analysis documents:
>   - [Phase 1: HTTP/1.1 Cleartext & TLS](BENCHMARK_ANALYSIS_PHASE1.md)
>   - [Phase 2: HTTP/2 Stream Multiplexing & HPACK](BENCHMARK_ANALYSIS_PHASE2.md)
>   - [Phase 3: HTTP/3 over QUIC](BENCHMARK_ANALYSIS_PHASE3.md)

---

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

Install Aegon system-wide (e.g., on Arch Linux via `pacman -S aegon` or building from source with `sudo cmake --install build`):

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
- [Comprehensive Benchmarks & Architecture](docs/guide/benchmarks.md)
- [Routing, Param Matching & Groups](docs/guide/routing.md)
- [Layered Configuration (`config.yml` & `.env`)](docs/guide/config.md)
- [C++26 Tasks & io_uring Kernel Engine](docs/guide/core/task.md)
- [Type-Safe SQL Engine & Static ORM](docs/guide/data/sql/schema.md)
- [Native Async Redis Client](docs/guide/data/redis/client.md)
- [API Gateway & Reverse Proxy](docs/guide/gateway.md)

---

## 📄 License

Aegon is licensed under the [MIT License](LICENSE).
