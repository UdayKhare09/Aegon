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
> Aegon is pioneering bleeding-edge C++26 language features, kernel `io_uring` multishot pipelines, and hardware-vectorized protocol engines:
> - **Aegon is in active early alpha development (`v0.1.a2`).**
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

## 📊 Performance & Benchmarks

Aegon is engineered for extreme throughput and deterministic sub-millisecond tail latencies. All benchmarks follow strict scientific methodology (physical CPU core pinning, 3s warm-up + 3 runs × 10s triplicate averages, un-cherry-picked):

| Protocol Suite | Aegon (C++26) | Swerver (Zig) | Actix-web (Rust, `rustls`) | Fiber (Go) | Drogon (C++) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **HTTP/1.1 Plaintext** | **547,145 req/s** | 468,613 req/s | 400,128 req/s | 352,123 req/s | 388,383 req/s |
| **HTTP/1.1 TLS (HTTPS)** | **423,902 req/s** | 382,901 req/s | 349,875 req/s | 327,720 req/s | 319,511 req/s |
| **HTTP/2 (Multiplexing)** | **1,778,204 req/s** | 460,518 req/s | 693,127 req/s | *UNSUPPORTED* | *UNSUPPORTED* |
| **HTTP/3 (QUIC / UDP)** | **336,441 req/s** | 109,571 req/s | *UNSUPPORTED* | *UNSUPPORTED* | *UNSUPPORTED* |

> 📖 **Full Scientific Reports**: See detailed breakdowns, latency percentiles, and reproduction instructions in the [Benchmark Documentation](docs/guide/benchmarks.md) and [`benchmarks/`](benchmarks/) suite directories.

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
- [API Gateway & Reverse Proxy](docs/guide/gateway.md)

---

## 📄 License

Aegon is licensed under the [MIT License](LICENSE).
