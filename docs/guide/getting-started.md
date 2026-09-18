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

### Installing Dependencies (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    liburing-dev \
    libssl-dev \
    libnghttp2-dev \
    libngtcp2-dev \
    libnghttp3-dev \
    libsqlite3-dev \
    libpq-dev
```

---

## Project Structure & CMake Setup

To use Aegon in your application, include Aegon as a subdirectory in your CMake project.

### Minimal `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyAegonApp LANGUAGES CXX)

# Enforce C++26 standard
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add Aegon subdirectory
add_subdirectory(extern/Aegon)

# Your executable
add_executable(my_app main.cpp)

# Link Aegon targets
target_link_libraries(my_app PRIVATE
    aegon_http
    aegon_core
    aegon_uuid
)
```

### Available CMake Targets

| Target | Description |
| :--- | :--- |
| `aegon_core` | Core runtime: `IoUring`, `EventLoop`, `BufferPool`, `Task<T>` coroutine primitives. |
| `aegon_http` | High-performance HTTP server, radix router, HTTP/1.1, HTTP/2, TLS, and RFC 7807 problem details. |
| `aegon_redis` | Native async Redis client (RESP3, Sentinel, Cluster, per-core pools, locks, streams). |
| `aegon_uuid` | High-speed SIMD-accelerated UUIDv4 generator and parser. |

---

## Writing Your First Server

Create a `main.cpp` file:

```cpp
#include <aegon/http/Server.h>
#include <iostream>

using namespace aegon::http;
using aegon::core::Task;

int main() {
    // 1. Create the server instance
    Server server;

    // 2. Define routes on the router
    server.router().get("/", [](Context& ctx) -> Task<void> {
        ctx.res().text("Hello, Aegon World!");
        co_return;
    });

    server.router().get("/ping", [](Context& ctx) -> Task<void> {
        ctx.res().json(R"({"status":"ok","engine":"io_uring"})");
        co_return;
    });

    // 3. Configure port and bind
    server.listen(8080);

    std::cout << "⚡ Aegon server listening on http://0.0.0.0:8080\n";

    // 4. Start the event loop
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
