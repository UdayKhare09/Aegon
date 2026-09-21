# Next-Generation Aegon ORM Architecture & Improvement Roadmap

## 1. Executive Summary & Benchmark Context

During high-concurrency database benchmarking (Phase 4 of the AppStack benchmark suite), we evaluated PostgreSQL async performance under 512 and 1,024 concurrent connections with core pinning parity (Server: Cores 0–1, Client: Cores 2–5, PostgreSQL: Cores 6–7):

- **Traditional Connection Pools (e.g., Actix `deadpool-postgres`)**: Hit an architectural ceiling at **17,233 req/s**. With sequential checkout (`checkout -> query -> await response -> release`), throughput is fundamentally bounded by connection count divided by round-trip latency:
  $$\text{Throughput} \le \frac{N_{\text{connections}}}{\text{Latency}_{\text{RTT}}}$$
- **Bounded Naive Micro-Parsers (e.g., Swerver Zig wire parser)**: Achieved high raw packet throughput by failing 87.7% of queries under load (**595,108 requests dropped with 503 `QueueFull`** out of 678,829 total requests). Its actual valid 200 OK throughput was only **~20,826 req/s**.
- **Aegon Pipeline Engine (C++26 + `io_uring`)**: Reached **26,125.45 clean req/s** with **0 errors (0.00%)** using pipelined execution slots, non-blocking socket polling, and zero-copy binary wire decoding.

This document outlines the architectural roadmap for bringing these high-performance primitives into Aegon's first-class ORM (`src/data/orm/`).

---

## 2. Core Architectural Pillars

```
┌───────────────────────────────────────────────────────────────────────┐
│                           Aegon Application                            │
│  co_await repo.find_where<User>(col("price").between(10, 50));        │
└──────────────────────────────────┬────────────────────────────────────┘
                                   │
                                   ▼
┌───────────────────────────────────────────────────────────────────────┐
│                 Compile-Time Reflection & Query Builder                │
│  - Zero-allocation SQL generator                                      │
│  - Compile-time binary parameter packing (C++26 static reflection)   │
└──────────────────────────────────┬────────────────────────────────────┘
                                   │
                                   ▼
┌───────────────────────────────────────────────────────────────────────┐
│             Shared-Nothing Pipelined Connection Pool                   │
│  - Thread-per-core (no mutex contention)                              │
│  - Multi-query socket pipelining (PQenterPipelineMode / Native Wire)  │
│  - Cooperative in-flight deque with Coroutine symmetric transfer      │
└──────────────────────────────────┬────────────────────────────────────┘
                                   │
                                   ▼
┌───────────────────────────────────────────────────────────────────────┐
│               Linux io_uring Engine (Async Polling & IO)              │
│  - co_await ring().poll(socket_fd, POLLIN / POLLOUT)                  │
│  - Zero thread-blocking, zero context-switch overhead                 │
└───────────────────────────────────────────────────────────────────────┘
```

### Pillar 1: Shared-Nothing Thread-Local Connection Architecture
- **Problem**: Global connection pools rely on `std::mutex`, atomics, or channels to checkout and return connections across worker threads, creating cache-line bouncing and lock contention at high request volumes.
- **Solution**: Each worker thread maintains its own independent `thread_local` connection pool (`PgPipelinePool`).
- **Benefits**:
  - Zero cross-thread synchronization.
  - Sockets stay pinned to the same CPU core as the event loop, maximizing CPU L1/L2 cache locality.

### Pillar 2: Native Wire-Level Pipelining
- **Problem**: Standard ORMs wait for a database query to complete before sending the next query on that connection. Under 512+ client concurrency, connections spend most of their time idling waiting for network packets.
- **Solution**: Multiplex concurrent coroutines onto shared connection slots using PostgreSQL pipeline mode:
  - Dispatches `PQsendQueryPrepared` + `PQpipelineSync` immediately into the socket buffer.
  - Suspends the coroutine via an `IoAwaiter` without blocking the worker thread.
  - Background slot reader coroutine drains results cooperatively and resumes the corresponding coroutines in FIFO order.
- **Benefits**:
  - 10x higher connection efficiency.
  - Handles thousands of concurrent HTTP requests with just 4–8 underlying PostgreSQL TCP sockets.

### Pillar 3: Zero-Copy Binary Wire Deserialization
- **Problem**: Default database drivers request results in Text format (`resultFormat = 0`), requiring PostgreSQL to format numbers/dates as strings and the client to parse strings back to integers/floats via `std::from_chars` or `std::stoi`.
- **Solution**: Request binary wire format (`resultFormat = 1`):
  - Integers and timestamps decoded with single SIMD byte-swaps (`__builtin_bswap32`, `__builtin_bswap64`).
  - Text and JSON/JSONB fields decoded as zero-copy `std::string_view` directly referencing the socket buffer.
  - Glaze integration (`glz::raw_json`) to pass JSONB objects directly to HTTP response buffers without intermediate heap allocation.

### Pillar 4: Compile-Time Model Reflection (C++26)
- **Problem**: Traditional ORMs inspect model schemas using runtime hash maps, string comparisons, and dynamic type descriptors.
- **Solution**: Generate schema mappings and query templates at compile time:
  - Automated binary parameter packing.
  - Compile-time prepared statement generation.
  - Zero runtime mapping overhead.

### Pillar 5: Safe Backpressure & Cooperative Queue Management
- **Problem**: Naive bounded buffers reject excess requests with HTTP 503 when saturated (as seen in Swerver's 88% error drop).
- **Solution**:
  - Implement adaptive backpressure: When connection pipelines reach optimal depth (e.g. 64–128 in-flight queries per slot), subsequent coroutines yield to the event loop rather than dropping.
  - Dynamically scale pipeline depth or apply cooperative coroutine scheduling to maintain 0.00% error rates under load spikes.

---

## 3. Proposed Public API Design

```cpp
#include <aegon/data/orm/PipelinedRepository.h>

struct Product {
    int64_t id;
    std::string_view name;
    std::string_view category;
    int32_t price;
    int32_t quantity;
    bool active;
    glz::raw_json tags;
};

// Automatic compile-time reflection
template <>
struct aegon::orm::Schema<Product> {
    static constexpr auto table_name = "items";
    static constexpr auto primary_key = &Product::id;
};

// In Route Handler:
aegon::core::Task<void> get_products(Context& ctx) {
    auto& repo = ctx.services().get<aegon::orm::PipelinedRepository<Product>>();
    
    // Automatically executes via thread-local pipeline slot with binary wire protocol
    auto items = co_await repo.query()
        .where(col("price").between(10, 50))
        .limit(50)
        .all();

    ctx.res().json(items);
}
```

---

## 4. Implementation Phasing

| Milestone | Deliverable | Priority |
| :--- | :--- | :---: |
| **Phase 1** | Abstract `PipelineSlot` and `run_slot_reader` from benchmark into reusable `aegon::data::pg::PipelineClient` | High |
| **Phase 2** | Add binary wire codec for common types (`int32`, `int64`, `bool`, `string_view`, `jsonb`) | High |
| **Phase 3** | Implement zero-allocation `QueryBuilder` producing parameterized binary payloads | Medium |
| **Phase 4** | Build compile-time schema reflection using Glaze metadata and C++26 reflection | Medium |
| **Phase 5** | Add connection health-checks, auto-reconnect, and multi-host failover | Production |
