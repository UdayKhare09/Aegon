---
layout: home

hero:
  name: "Aegon"
  text: "Ultra High-Performance Linux-Native Web Framework"
  tagline: "C++26 Coroutines • io_uring Kernel Engine • Zero-Reflection ORM • Native Async Redis"
  actions:
    - theme: brand
      text: Get Started
      link: /guide/getting-started
    - theme: alt
      text: Benchmarks (3.6M req/s) 🚀
      link: /guide/benchmarks
    - theme: alt
      text: View on GitHub
      link: https://github.com/UdayKhare09/Aegon

features:
  - icon: ⚡
    title: Linux-Native io_uring Engine
    details: Complete kernel bypass and zero-syscall runtime leveraging multishot accept, multishot recv, buffer rings, and optional SQPOLL kernel worker threads.
  - icon: 🔄
    title: Pure C++26 Coroutines
    details: Symmetric transfer coroutines via Task<T> with zero heap allocations in hot paths, eliminating thread pool exhaustion and context switching.
  - icon: 🏆
    title: Verified World-Class Speed
    details: Outperforms Actix-web, Drogon, and Axum across HTTP/1.1 (912k req/s), HTTP/2 (3.6M req/s), and HTTP/3 QUIC (183k req/s) with sub-5ms p99 latency.
  - icon: 🛡️
    title: Compile-Time Static ORM
    details: Type-safe SQL builder with zero reflection overhead. Fluent expressions, eager relation loading, migrations, and atomic transactions.
  - icon: 🚀
    title: Native Async Redis Engine
    details: Built from scratch on io_uring. Supports standalone, Sentinel high availability, and multi-node Redis Cluster with per-core connection pools.
  - icon: 🌐
    title: Full-Stack Protocol Support
    details: First-class native HTTP/1.1 pipelining, HTTP/2 stream multiplexing with HPACK, and industry-pioneering HTTP/3 over QUIC (RFC 9000 / 9114).
---

<div class="vp-doc" style="max-width: 100%; margin: 40px auto 0 auto; padding: 0 48px;">

## 📊 Proven Performance at Scale

Aegon was evaluated on bare-metal hardware (**AMD Ryzen 5 7600X, 6 Zen 4 Cores @ 5.3GHz, AVX-512, Linux 6.13**) against **Drogon** (C++), **Actix-web** (Rust), and **Axum** (Rust) on a standard `GET /health` endpoint:

| Protocol Tier | Aegon (C++26) | Drogon (C++17) | Actix-web (Rust) | Axum (Rust) | Aegon Advantage |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **HTTP/1.1 Cleartext (Peak)** | **912,374 req/s** 🏆 | 836,580 req/s | 854,544 req/s | 824,885 req/s | **+6.8% over #2** |
| **HTTP/1.1 TLS (Peak)** | **886,920 req/s** 🏆 | 798,410 req/s | 812,450 req/s | 785,120 req/s | **+9.2% over #2** |
| **HTTP/2 Cleartext (`h2c`)** | **3,596,346 req/s** 🏆 | `UNSUPPORTED` | `UNSUPPORTED` | 605,585 req/s | **6.27x faster** |
| **HTTP/2 TLS (`h2`)** | **3,480,864 req/s** 🏆 | `UNSUPPORTED` | 1,063,276 req/s | 707,069 req/s | **3.27x faster** |
| **HTTP/3 over QUIC (`h3`)** | **183,574 req/s** 🏆 | `UNSUPPORTED` | `UNSUPPORTED` | `UNSUPPORTED` | **Only Framework** |
| **p99 Tail Latency (H2 Stress)** | **4.28 ms** 🏆 | N/A | 45.45 ms | 47.58 ms | **10.6x lower tail** |

::: tip In-Depth Analysis & Disclaimers
Read the complete methodological details, latency percentiles (p50/p99), memory RSS profiles, and official disclaimers in the **[Comprehensive Benchmark Guide &rarr;](/guide/benchmarks)**
:::

---

## 10-Line "Hello World"

A complete, asynchronous, io_uring-backed HTTP server in modern C++26:

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
