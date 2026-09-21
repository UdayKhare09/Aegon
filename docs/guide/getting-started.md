# Getting Started

Welcome to **Aegon**, a next-generation, asynchronous C++26 web framework and compile-time ORM engineered specifically for modern Linux systems.

Aegon uses Linux `io_uring` kernel features (multishot accept/recv, provided buffer rings, zero-copy socket operations) paired with C++26 coroutines (`co_await`, symmetric transfer) to provide top-tier throughput with zero-overhead async I/O.

---

## System Requirements

Because Aegon directly drives modern Linux kernel interfaces and modern C++ standard features, ensure your environment meets the following requirements:

| Requirement | Minimum Version | Notes |
| :--- | :--- | :--- |
| **Linux Kernel** | `6.0+` (Recommended: `6.5+`) | Requires `io_uring` features: `IORING_FEAT_FAST_POLL`, multishot accept/recv, provided buffer rings (`BGID`). |
| **C++ Compiler** | `GCC 14+` or `Clang 19+` | Full C++26 language flag (`-std=c++26`) and coroutine support. |
| **Build System** | `CMake 3.20+` | Standard CMake toolchain with Ninja or Make. |
| **System Libraries** | `liburing`, `OpenSSL`, `libnghttp2` | Core dependencies for ring I/O, TLS, and HTTP/2 framing. |

### Installing Dependencies (Arch Linux)

```bash
sudo pacman -Syu base-devel cmake ninja liburing openssl glaze postgresql-libs sqlite nghttp2 ngtcp2 nghttp3
```

### Installing Aegon

#### Option A: Arch Linux Package
```bash
# Build and install the Arch package
cd packaging/arch
makepkg -si
```

#### Option B: From Source via CMake
```bash
git clone https://github.com/UdayKhare09/Aegon.git
cd Aegon
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

---

## Quick Start via Aegon CLI (Recommended)

The fastest way to get up and running is using the `aegon` CLI tool:

```bash
# 1. Verify your system environment and kernel capabilities
aegon doctor

# 2. Scaffold a new production-ready C++26 service
aegon new my_service

# 3. Enter directory and launch the application
cd my_service
aegon run
```

This immediately spins up a high-performance HTTP service on `http://0.0.0.0:8080` configured with structured YAML settings, `.env` overrides, and C++26 coroutines.

---

## Manual CMake Integration

If integrating Aegon into an existing codebase, use CMake's `find_package(Aegon REQUIRED)`:

### Modern `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.25)
project(MyAegonApp LANGUAGES CXX)

# Enforce C++26 standard
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimize for modern CPU architecture
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -march=native -Wall -Wextra)
endif()

# Find Aegon installed package
find_package(Aegon REQUIRED)

# Your executable
add_executable(my_app src/main.cpp)

# Link modular Aegon component targets
target_link_libraries(my_app PRIVATE
    Aegon::http
    Aegon::core
    Aegon::config
    Aegon::orm     # Optional: if using SQL ORM
    Aegon::redis   # Optional: if using Redis
)
```

### Modular CMake Targets

Aegon exports strictly modular targets:

| Target | Description |
| :--- | :--- |
| `Aegon::core` | Core runtime: `IoUring`, `EventLoop`, `BufferPool`, `Task<T>` coroutine primitives, and SIMD UUID. |
| `Aegon::http` | High-performance HTTP/1.1, HTTP/2, and HTTP/3 QUIC server, radix router, and middleware. |
| `Aegon::orm` | Unified SQL ORM, query builder, transactions, schema migrations, SQLite3, and PostgreSQL drivers. |
| `Aegon::redis` | Native async Redis client (RESP3, Sentinel, Cluster, per-core pools, distributed locks, streams). |
| `Aegon::memory` | Async in-process MemStore — Redis alternative running on its own thread with LRU eviction and TTL sweep. |
| `Aegon::config` | Layered YAML 1.2 and `.env` configuration builder with `${VAR:default}` expansion. |
| `Aegon::gateway` | High-performance reverse proxy, dynamic routing, and cluster circuit breakers. |

---

## Writing Your First Server

Create `src/main.cpp`:

```cpp
#include <aegon/http/Server.h>
#include <aegon/core/Task.h>
#include <iostream>

using namespace aegon::http;
using aegon::core::Task;

int main() {
    // 1. Create the server instance
    Server server;

    // 2. Define asynchronous coroutine route
    server.router().get("/", [](Context& ctx) -> Task<void> {
        ctx.res().text("Hello, Aegon World!");
        co_return;
    });

    // 3. Define synchronous route (zero-overhead)
    server.router().get("/ping", [](Context& ctx) {
        ctx.res().json(R"({"status":"ok","engine":"io_uring"})");
    });

    // 4. Configure port and bind
    server.listen(8080);
    std::cout << "⚡ Aegon server listening on http://0.0.0.0:8080\n";

    // 5. Start the event loop
    server.run();
    return 0;
}
```

---

## Building and Running

Configure and compile your application:

```bash
mkdir build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
ninja

# Run your server
./my_app
```

In another terminal, test the endpoints using `curl`:

```bash
$ curl -i http://localhost:8080/
HTTP/1.1 200 OK
Content-Type: text/plain; charset=utf-8
Content-Length: 19

Hello, Aegon World!

$ curl -i http://localhost:8080/ping
HTTP/1.1 200 OK
Content-Type: application/json; charset=utf-8
Content-Length: 35

{"status":"ok","engine":"io_uring"}
```

---

## Next Steps

Now that your first server is running:

- Dive into [Routing & Groups](/guide/routing) to learn dynamic path parameters (`:id`), wildcards, and route grouping.
- Inspect incoming requests with [HTTP Request](/guide/request).
- Build rich responses with [HTTP Response](/guide/response).
- Learn automatic DTO parsing and validation in [Context & Binding](/guide/context).
