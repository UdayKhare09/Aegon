<p align="center">
  <img src="name_logo.svg" alt="AEGON" width="460">
</p>

<p align="center">
  <strong>High-Performance C++23 Asynchronous Web &amp; Data Framework</strong>
</p>

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey.svg)]()
[![Async Engine](https://img.shields.io/badge/I%2FO-io__uring%20%7C%20epoll%20%7C%20IOCP-orange.svg)]()
[![HTTP Arena H/1.1](https://img.shields.io/endpoint?url=https://http-arena.com/badge/aegon/h1.json)](https://http-arena.com/#type=emerging,flagship&tuned=0)
[![HTTP Arena H/2](https://img.shields.io/endpoint?url=https://http-arena.com/badge/aegon/h2.json)](https://http-arena.com/#type=emerging,flagship&tuned=0)
[![HTTP Arena WebSocket](https://img.shields.io/endpoint?url=https://http-arena.com/badge/aegon/ws.json)](https://http-arena.com/#type=emerging,flagship&tuned=0)
[![HTTP Arena H/3](https://img.shields.io/endpoint?url=https://http-arena.com/badge/aegon/h3.json)](https://http-arena.com/#type=emerging,flagship&tuned=0)

Aegon is a modern, unified, thread-per-core C++23 application framework engineered for extreme throughput, low latency, and ergonomic developer experience. Designed from the ground up around standard C++ coroutines (`co_await`), Aegon eliminates callback spaghetti while providing native support for modern networking protocols, an integrated reflection-free SQL ORM with multi-tier L2 caching, a high-throughput Redis client, and an asynchronous HTTP/1.1, HTTP/2, and HTTP/3 client.

---

## Quickstart & Code Example

### Modern HTTP REST API with Coroutines

Build a high-performance REST endpoint with JSON request/response serialization and C++23 coroutines:

```cpp
#include <aegon/http/Server.h>
#include <aegon/core/Task.h>

using namespace aegon;

// Define your request/response data models
struct CreateUserRequest {
    std::string username;
    std::string email;
};

struct UserResponse {
    uint64_t id;
    std::string username;
    std::string email;
    std::string status;
};

int main() {
    http::Router router;

    // Plaintext Health Check
    router.get("/health", [](http::Context& ctx) -> core::Task<void> {
        ctx.res().status(http::StatusCode::Ok).text("OK");
        co_return;
    });

    // Coroutine-based JSON endpoint
    router.post("/api/v1/users", [](http::Context& ctx) -> core::Task<void> {
        // Automatically bind and parse JSON request body
        auto req_body = ctx.bind_json<CreateUserRequest>();
        if (!req_body) {
            // Automatically returns 422 Unprocessable Entity if JSON fails validation
            co_return;
        }

        UserResponse response{
            .id = 1001,
            .username = std::move(req_body->username),
            .email = std::move(req_body->email),
            .status = "active"
        };

        // Serialize response using Glaze compile-time reflection
        ctx.res().status(http::StatusCode::Created).json(response);
        co_return;
    });

    // Start HTTP server on port 8080 across all available CPU cores
    http::Server server(std::move(router));
    server.listen(8080, "0.0.0.0");

    // Run thread-per-core event loop cluster
    server.run(std::thread::hardware_concurrency());
    return 0;
}
```

---

## Performance & Benchmarks

All results are produced by the independent [HttpArena](https://http-arena.com) benchmark platform under identical, scientifically controlled conditions.

### Reference Hardware & Topology

- **Host OS**: Linux (Ubuntu 24.04 / 25.04, kernel ≥ 6.1)
- **Container Environment**: Docker container running Arch Linux (`archlinux:latest`, GCC 14)
- **CPU**: **AMD Ryzen Threadripper PRO 3995WX** (64 Physical Cores, 128 SMT Threads, Zen 2, 2.7 GHz base / 4.2 GHz boost, 280W TDP)
- **Memory**: 257 GB DDR4-3200 (8-channel symmetrical DRAM bandwidth ~205 GB/s, 1 NUMA Node NPS1)
- **Cache**: 256 MB L3 (16 CCXs × 16 MB exclusive L3), 32 MB L2, 4 MB L1
- **CPU Partitioning**: Strict core isolation via cpusets — **32 physical cores / 64 threads** dedicated to the server container (`cpuset 0-31,64-95`, CCX 0–7) and **32 physical cores / 64 threads** dedicated to load generators (`cpuset 32-63,96-127`, CCX 8–15).
- **Network & OS Tuning**: Realistic loopback MTU set to 1500 bytes (eliminating 64KB loopback shortcut artifacts), performance CPU governor, `somaxconn` 65535, optimized TCP/UDP buffers.

---

### 1. Throughput & Concurrency Benchmarks (All Profiles)

Peak throughput, latency, and concurrency scaling across all 28 throughput profiles:

| Profile | Connections | Throughput | Avg Latency | p99 Latency | Position (In HttpArena) |
| --- | :---: | :---: | :---: | :---: | :---: |
| `baseline-h2c` | 1,024 | **14,444,079 req/s** | 5.31 ms | 20.57 ms | **#1 of 22** |
| `baseline-h2c` | 4,096 | **13,802,882 req/s** | 18.48 ms | 67.71 ms | **#1 of 22** |
| `baseline-h2c` | 256 | **13,103,154 req/s** | 1.56 ms | 8.24 ms | **#1 of 22** |
| `json-h2c` | 1,024 | **4,000,354 req/s** | 7.80 ms | 31.34 ms | **#1 of 22** |
| `json-h2c` | 4,096 | **3,223,981 req/s** | 39.28 ms | 108.52 ms | **#1 of 22** |
| `baseline-h2` | 1,024 | **13,414,881 req/s** | 5.71 ms | 89.62 ms | **#1 of 27** |
| `baseline-h2` | 256 | **12,529,960 req/s** | 1.58 ms | 13.05 ms | **#1 of 27** |
| `echo-ws-pipeline` | 4,096 | **63,623,190 msg/s** | 1.03 ms | 1.36 ms | **#1 of 27** |
| `echo-ws-pipeline` | 512 | **63,184,451 msg/s** | 129 µs | 237 µs | **#1 of 27** |
| `echo-ws-pipeline` | 16,384 | **58,668,976 msg/s** | 4.45 ms | 5.51 ms | **#1 of 27** |
| `echo-ws` | 4,096 | **4,283,047 msg/s** | 956 µs | 1.30 ms | **#1 of 30** |
| `echo-ws` | 512 | **4,207,888 msg/s** | 121 µs | 225 µs | **#1 of 30** |
| `echo-ws` | 16,384 | **3,951,014 msg/s** | 4.13 ms | 5.17 ms | **#1 of 30** |
| `echo-ws-limited` | 512 | **2,189,840 msg/s** | 122 µs | 384 µs | **#1 of 21** |
| `echo-ws-limited` | 4,096 | **2,545,648 msg/s** | 1.05 ms | 1.75 ms | **#4 of 21** |
| `pipelined` | 4,096 | **27,608,511 req/s** | 2.37 ms | 3.14 ms | **#3 of 77** |
| `baseline` | 4,096 | **3,996,171 req/s** | 1.02 ms | 1.71 ms | **#1 of 83** |
| `async` (10ms delay) | 32,000 | **2,647,330 req/s** | 12.01 ms | 13.10 ms | **#1 of 79** |
| `limited-conn` (10 req/conn) | 4,096 | **2,597,100 req/s** | 1.45 ms | 2.82 ms | **#2 of 83** |
| `baseline-h3` | 64 | **2,981,569 req/s** | 1.78 ms | 4.82 ms | **#3 of 16** |
| `static-h3` | 64 | **309,158 req/s** | 15.86 ms | 48.33 ms | **#2 of 16** |
| `json-tls` | 4,096 | **1,290,079 req/s** | 3.12 ms | 56.71 ms | **#2 of 80** |
| `json-comp` | 4,096 | **962,211 req/s** | 4.21 ms | 9.09 ms | **#1 of 79** |
| `json-comp` | 16,384 | **935,723 req/s** | 17.34 ms | 25.40 ms | **#1 of 79** |
| `8gbit` (Stream echo) | 512 | **49,349 req/s** (514 MB/s) | 119.9 µs | 169.0 µs | **#25 of 79** |
| `static-h2` | 256 | **516,574 req/s** | 17.47 ms | 45.78 ms | **#6 of 26** |
| `static-tls` | 1,024 | **499,850 req/s** | 2.30 ms | 31.85 ms | **#6 of 42** |
| `static-h2` | 1,024 | **461,672 req/s** | 67.66 ms | 140.84 ms | **#5 of 26** |

---

### 2. Latency & CPU Efficiency Benchmarks (Fixed-Rate Paced Load)

Unlike peak-throughput tests, **HttpArena Latency profiles** fix the offered load rate using `zrk` (with coordinated-omission correction) over 1,024 persistent connections. Because every completed server serves the same rate, rankings are determined by **CPU cost per request** (measured exactly in microseconds from cgroup v2 `cpu.stat`), **mean queueing latency**, and **p99 tail latency**:

| Profile | Target Rate | Achieved Rate | CPU Cost / Req | Total CPU | Mean Latency | p99 Latency | Score (Position) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **`latency-1m`** (Saturation) | 1,000,000 req/s | 997,511 req/s (99.8%) | **14.92 µs / req** | 1586% | **254 µs** | **170 µs** | **87.5 (#1 of 82)** |
| **`latency-10k`** (Idle Overhead) | 10,000 req/s | 9,980 req/s (99.8%) | **30.44 µs / req** | 29% | **70 µs** | **98 µs** | **80.0 (#11 of 82)** |
| **`latency-500k-8cpu`** (CPU Cap) | 500,000 req/s | 483,013 req/s (96.6%) | **16.36 µs / req** | 814% | **294 ms** | **2,255 ms** | **44.5 (#1 of 80)** |

#### Key Takeaways

- **`latency-1m` (#1 Overall)**: At 1 Million requests/sec, Aegon spent only **14.9 µs of CPU per request** (beating Swerver at 22.9 µs, Actix-web at 24.9 µs, Drogon at 32.1 µs, and Fiber at 38.3 µs), keeping p99 latency at a sub-millisecond **170 µs**.
- **`latency-10k`**: Measures the idle standing cost of the framework when nearly unloaded. Aegon consumes just **29% of a single core** across 1,024 open idle connections, maintaining a **70 µs mean latency**.
- **`latency-500k-8cpu` (#1 Overall)**: Measures behavior under severe CPU constraints (server restricted to 4 physical cores / 8 SMT threads). While other frameworks saw their throughput collapse to 70–88% rate delivery, Aegon held **96.6%** rate at **16.4 µs/req**.

---

## Prerequisites & Dependencies

### Toolchain Requirements

- **C++ Compiler**: GCC 13+, Clang 17+, or MSVC 2022 (v19.38+) with full C++23 coroutine support.
- **Build System**: CMake 3.20+ and Ninja (recommended).

### Core Libraries

| Component | Library | Purpose |
| :--- | :--- | :--- |
| **Serialization** | [Glaze](https://github.com/stephenberry/glaze) | Compile-time reflection and ultra-fast JSON serialization |
| **Cryptography** | OpenSSL (>= 3.0) | TLS 1.3, ALPN negotiation, and cryptographic primitives |
| **Async I/O (Linux)** | `liburing` | High-efficiency Linux `io_uring` system call interface |
| **HTTP/2** | `nghttp2` | RFC 7540 frame parsing and HPACK compression |
| **HTTP/3 & QUIC** | `ngtcp2` & `nghttp3` | RFC 9000 QUIC transport and RFC 9114 HTTP/3 handling |
| **Compression** | `libdeflate` | Whole-buffer deflate / gzip compression |
| **Databases** | `PostgreSQL` (libpq) & `SQLite3` | Database engine connectivity |

---

## Building & Installation

### 1. Clone the Repository

```bash
git clone https://github.com/UdayKhare09/Aegon.git
cd aegon
```

### 2. Configure with CMake

```bash
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DAEGON_BUILD_TESTS=ON \
    -DAEGON_BUILD_SAMPLES=ON
```

#### Build Configuration Flags

| Flag | Default | Description |
| :--- | :---: | :--- |
| `AEGON_BUILD_TESTS` | `ON` | Build the comprehensive CTest test suite |
| `AEGON_BUILD_SAMPLES` | `ON` | Build reference sample services (`samples/adv`, `samples/sse`) |
| `AEGON_BUILD_BENCHMARKS` | `ON` | Build the unified performance benchmarking suite |

### 3. Compile the Framework

```bash
cmake --build build -j$(nproc)
```

### 4. Run Test Suite

```bash
docker compose -f docker-compose.test.yml up -d # for live Redis and Postgres tests
ctest --test-dir build --output-on-failure
```

### 5. Install System-Wide (Optional)

```bash
sudo cmake --install build
```

---

## License

Aegon is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.
