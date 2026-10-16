---
layout: home

hero:
  name: "Aegon"
  text: "Ultra High-Performance Cross-Platform Web Framework"
  tagline: "C++26 Coroutines • io_uring, epoll & Windows IOCP • Compile-Time ORM • Native Redis"
  actions:
    - theme: brand
      text: Get Started
      link: /guide/getting-started
    - theme: alt
      text: Benchmarks Matrix
      link: /guide/benchmarks
    - theme: alt
      text: I/O Engine Architecture
      link: /guide/core/io-engine
    - theme: alt
      text: View on GitHub
      link: https://github.com/UdayKhare09/Aegon

features:
  - icon: ⚡
    title: Multi-Backend I/O Engines
    details: Zero-allocation IoEngine with Linux 6.1+ io_uring (multishot & PBUF_RING), automatic epoll fallback (Linux < 6.1, Docker, WSL2), and native Windows NT IOCP via MSVC.
  - icon: 🔄
    title: Pure C++26 Coroutines
    details: Symmetric transfer coroutines via Task<T> with zero heap allocations on hot paths, eliminating thread pool exhaustion and context switching.
  - icon: 🛠️
    title: First-Class Developer CLI
    details: Built-in developer tooling with project scaffolding (aegon new), system environment diagnostics (aegon doctor), and streamlined build/run commands.
  - icon: 🛡️
    title: Compile-Time Static ORM
    details: Type-safe SQL builder with zero reflection overhead. Fluent expressions, eager relation loading, migrations, and atomic transactions.
  - icon: 🚀
    title: Native Async Redis Engine
    details: Built from scratch with per-core connection pools. Supports standalone, Sentinel high availability, and multi-node Redis Cluster.
  - icon: 🌐
    title: Full-Stack Protocol Suite
    details: Native HTTP/1.1 pipelining, HTTP/2 multiplexing with HPACK, RFC 6455 WebSockets with SIMD unmasking, and HTTP/3 over QUIC (RFC 9000 / 9114).
---

<div class="vp-doc" style="max-width: 100%; margin: 40px auto 0 auto; padding: 0 48px;">

::: warning Project Status: Version 0.1.a2 (Early Alpha)
Aegon is in active early development (`v0.1.a2`). **Public APIs and internal behaviors can and will have breaking changes in future releases** as paradigms evolve towards stability.
:::

---

## ⚡ Cross-Platform Performance (2 Dedicated Cores)

Aegon delivers peak throughput across Linux and Windows without compromising safety or RFC compliance:

| Protocol Suite | Linux `io_uring` | Linux `epoll` | Windows `IOCP` (MSVC) | Verification Status |
| :--- | :--- | :--- | :--- | :---: |
| **HTTP/1.1 Plaintext** | **528,851 req/s** | **454,290 req/s** | **94,431 req/s** | 100% Passed |
| **HTTP/1.1 TLS 1.3** | **398,003 req/s** | **350,448 req/s** | **86,655 req/s** | 100% Passed |
| **HTTP/2 Multiplexing** | **1,350,621 req/s** | **1,316,487 req/s** | **355,548 req/s** | 100% Passed |
| **HTTP/3 (QUIC)** | **836,720 req/s** | **873,842 req/s** | **283,238 req/s** | **0% packet loss** |
| **Test Suite Pass Rate** | **46 / 46 (100%)** | **46 / 46 (100%)** | **41 / 41 (100%)** | **Zero Regressions** |

---

## 10-Line "Hello World"

A complete, asynchronous, multi-backend HTTP server in modern C++26:

```cpp
#include <aegon/http/Server.h>

using namespace aegon::http;

int main() {
    Server server;

    server.router().get("/hello", [](Context& ctx) -> aegon::core::Task<void> {
        ctx.res().text("Hello from Aegon!");
        co_return;
    });

    server.listen(8080).run();
    return 0;
}
```

```bash
# Test with curl
curl http://localhost:8080/hello
# Output: Hello from Aegon!
```

</div>

