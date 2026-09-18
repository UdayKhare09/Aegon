# Aegon HyperStore — Advanced Microservice Sample

A production-grade, multi-file reference architecture demonstrating nearly all features of the **Aegon** C++26 high-performance asynchronous web framework and compile-time ORM without middleware.

---

## Architecture Overview

This sample implements an E-Commerce & Flash-Sale Microservice demonstrating clean separation of concerns and dependency injection:

```
samples/adv/
├── CMakeLists.txt
├── README.md
├── test_adv_sample.sh
├── include/
│   ├── config/
│   │   └── AppConfig.h
│   ├── dtos/
│   │   ├── AuthDto.h
│   │   ├── ProductDto.h
│   │   └── OrderDto.h
│   ├── models/
│   │   ├── User.h
│   │   ├── Product.h
│   │   └── Order.h
│   ├── services/
│   │   ├── CatalogService.h
│   │   ├── OrderService.h
│   │   ├── LeaderboardService.h
│   │   └── EventStreamWorker.h
│   └── handlers/
│       ├── AuthHandler.h
│       ├── CatalogHandler.h
│       ├── OrderHandler.h
│       ├── LeaderboardHandler.h
│       └── StreamHandler.h
└── src/
    ├── main.cpp
    ├── services/
    │   ├── CatalogService.cpp
    │   ├── OrderService.cpp
    │   ├── LeaderboardService.cpp
    │   └── EventStreamWorker.cpp
    └── handlers/
        ├── AuthHandler.cpp
        ├── CatalogHandler.cpp
        ├── OrderHandler.cpp
        ├── LeaderboardHandler.cpp
        └── StreamHandler.cpp
```

---

## Features Demonstrated

1. **Complete Component Decoupling & ServiceRegistry**:
   - `aegon_http`, `aegon_orm`, and `aegon_redis` are compiled as separate, decoupled libraries.
   - `http::Server` uses `server.provide<T>(service)` to register dependencies in `ServiceRegistry`.
   - In route handlers, dependencies are injected cleanly via `ctx.service<T>()` with zero god-object context bloat.

2. **Compile-Time Relational ORM (`aegon::data::orm::sql`)**:
   - Declarative schemas with compile-time table definitions (`User`, `UserProfile`, `Product`, `Order`).
   - Automated DDL schema generation (`generate_ddl<T>()`).
   - Fluent query builders (`from<Product>().limit(...).offset(...)`).
   - **Relational Mappings (1:1 and 1:N)**:
     - `User` has `HasOne<UserProfile> profile` (`.has_one(&User::profile, &UserProfile::user_id)`).
     - `User` has `HasMany<Order> orders` (`.has_many(&User::orders, &Order::user_id)`).
     - `Product` has `HasMany<Order> orders` (`.has_many(&Product::orders, &Order::product_id)`).
     - Eager loading using `.include(&User::orders)` automatically populating related nested collections.
   - Atomic transactions (`db.transaction([&](Transaction& tx) -> Task<void> { ... })`).
   - **Optimistic Concurrency Control (OCC)**: `table<Product>().version(&Product::version)` guarantees zero-lock concurrency safety and eliminates lost updates during flash sales.

3. **Asynchronous Redis & Distributed Systems (`aegon::data::redis`)**:
   - Thread-per-core `RedisClient` integrated with `io_uring`.
   - **Distributed Mutex / Locking (`RedisLock`)**: Guards inventory decrements under high contention.
   - **Lua Scripting (`eval`)**: Atomic updates to Redis Sorted Sets for real-time trending leaderboards (`ZINCRBY`).
   - **Redis Streams (`xadd` / `xread`)**: Asynchronous event publishing and background consumer workers.
   - **L2 Cache Aside**: High-performance key-value caching with TTL for catalog items.

4. **HTTP Protocol & SIMD Routing (`aegon::http`)**:
   - HTTP/1.1, HTTP/2, and HTTP/3 support over `io_uring`.
   - SIMD-accelerated routing with URL path parameters (`/api/v1/users/:id`, `/api/v1/products/:id`, `/api/v1/orders/:id`).
   - Streaming responses (Server-Sent Events / SSE) over persistent HTTP connections.

5. **Data Transfer Objects (DTOs) & Validation (`aegon::validation`)**:
   - Glaze-accelerated zero-copy JSON serialization.
   - Compile-time and runtime field validation (`required()`, `min_len()`, `email()`, `min()`).
   - Automatic RFC 7807 Problem Details generation with HTTP 422 Unprocessable Entity.

---

## Building & Running

### Build
From repository root:
```bash
cmake -B build -G Ninja
cmake --build build -j$(nproc)
```

The sample binary is generated at `build/samples/adv/aegon_adv_sample`.

### Run
```bash
./build/samples/adv/aegon_adv_sample 8080
```

### End-to-End Automated Test Verification
```bash
./samples/adv/test_adv_sample.sh
```
This script tests health checks, DTO validation rejections, user registration, catalog management, OCC flash-sale order placement, and live SSE streaming against a running instance.
