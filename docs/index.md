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
      text: CLI Tooling
      link: /guide/cli
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
  - icon: 🛠️
    title: First-Class Developer CLI
    details: Built-in developer tooling with project scaffolding (aegon new), system environment diagnostics (aegon doctor), and streamlined build/run commands.
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

::: warning Project Status: Version 0.1.a2 (Early Alpha)
**Crafted by Human Vision, Synthesized with AI Assistance**
Aegon is an experimental, bleeding-edge framework **conceived, architected, and guided by human engineering, with the majority of the code written in deep collaboration with advanced AI systems**. 

Aegon is in active early development (`v0.1.a2`). **Public APIs and internal behaviors can and will have breaking changes in future releases** as paradigms evolve towards stability.
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
