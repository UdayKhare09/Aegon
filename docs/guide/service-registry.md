# Service Registry & Dependency Injection

Modern web services depend on shared components: database connections, Redis clusters, background job queues, external payment APIs, and telemetry collectors.

Aegon provides a built-in, ultra-high-performance dependency injection container called **`ServiceRegistry`**. It allows you to bind services by type and optional name at startup, automatically freezes the registry upon `server.run()`, and resolves dependencies inside coroutine route handlers via a **Zero-Lock Hot Path**.

---

## How It Works

`ServiceRegistry` assigns a sequential, compile-time static type ID (`detail::service_type_id<T>()`) to each registered type. 

1. **Startup Phase (Dynamic Registration)**:
   Services are registered dynamically on the server prior to listening. Registration is protected by a thread-safe mutex.
2. **Runtime Phase (Frozen Registry & Zero-Lock Hot Path)**:
   When `server.run()` or `server.run(threads)` starts, `services_->freeze()` is invoked. The registry constructs immutable, direct-indexed pointer tables. Lookups inside route handlers (`ctx.service<T>()`) execute with **zero mutex locks, zero hash lookups, zero allocations, and zero cache-line contention** across all CPU cores.

```
       [ Server Startup / Bootstrapping ]
                       │
                       ▼
       server.provide<SqlDatabaseClient>(master_db)
       server.provide<SqlDatabaseClient>("replica", read_db)   <-- Keyed Service
       server.provide<PerCoreRedisClient>(redis)
                       │
                       ▼  server.run()
             [ Frozen Registry ]
     Direct Flat Array (Unkeyed) + Immutable Hash Table (Keyed)
                       │
                       ▼  Request Dispatched to Worker Core
              [ In Route Handler ]
       auto& db = ctx.service<SqlDatabaseClient>();            <-- ZERO LOCK O(1)
       auto& replica = ctx.service<SqlDatabaseClient>("replica");
```

---

## Registering Services

Services can be registered unkeyed (default) or with distinct string keys:

### 1. In-Place Construction (`provide<T, Args...>`)

```cpp
// Constructs std::make_shared<OrderService>(apiKey, timeoutMs)
server.provide<OrderService>("api_key_secret", 5000);
```

### 2. Shared Pointer Registration (`provide<T>`)

```cpp
// Register PerCoreRedisClient for zero-contention thread-affinity Redis
auto redis = std::make_shared<PerCoreRedisClient>(RedisNodeConfig{.host = "127.0.0.1", .port = 6379});
server.provide<PerCoreRedisClient>(redis);
```

### 3. Keyed / Named Services (`provide<T>(name, ...)` & `provide_named<T>()`)

Register multiple instances of the same type under distinct names (e.g. primary vs read-replica databases, multi-tier caches):

```cpp
// Register primary write database and read replica database
server.provide<SqlDatabaseClient>("primary", primary_db);
server.provide<SqlDatabaseClient>("replica", replica_db);

// Construct named cache in-place
server.provide_named<RedisClient>("session_cache", "10.0.0.1", 6380);
```

---

## Accessing Services in Handlers

Inside any route handler, access services through `ctx`:

### 1. Required Service (`ctx.service<T>()` or `ctx.service<T>(name)`)

Retrieves a direct reference `T&`. If the service is missing, throws a descriptive `std::runtime_error`:

```cpp
server.router().post("/orders", [](Context& ctx) -> Task<void> {
    // Zero-lock O(1) retrieval
    auto& orders = ctx.service<OrderService>();
    auto& master_db = ctx.service<SqlDatabaseClient>("primary");

    // Process order...
    ctx.res().status(StatusCode::Created).text("Order placed");
    co_return;
});
```

### 2. Optional Service (`ctx.try_service<T>()` or `ctx.try_service<T>(name)`)

Returns a raw pointer `T*`, or `nullptr` if the service was not registered:

```cpp
server.router().get("/metrics", [](Context& ctx) -> Task<void> {
    if (auto* stats = ctx.try_service<MetricsCollector>()) {
        ctx.res().json(stats->snapshot());
    } else {
        ctx.res().text("Metrics collection is disabled");
    }
    co_return;
});
```

### 3. Check Existence (`ctx.has_service<T>()` or `ctx.has_service<T>(name)`)

```cpp
if (ctx.has_service<SqlDatabaseClient>("replica")) {
    // Route read traffic to read replica...
}
```

---

## `ServiceRegistry` API Reference

If you interact with `ServiceRegistry` directly (e.g. inside `.on_start()` hooks or custom sub-systems):

```cpp
ServiceRegistry& registry = server.services();
```

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `register_service<T>(shared_ptr<T>)` | `void` | Registers an unkeyed instance of `T`. Throws if frozen. |
| `register_service<T>(name, shared_ptr<T>)` | `void` | Registers a named/keyed instance of `T`. Throws if frozen. |
| `freeze()` | `void` | Freezes the registry and activates the zero-lock hot-path tables. (Idempotent). |
| `is_frozen()` | `bool` | Returns `true` if the registry has been frozen. |
| `has<T>(name = "")` | `bool` | Returns `true` if a service of type `T` (and optional `name`) exists. |
| `get<T>(name = "")` | `T*` | Returns raw pointer to `T`, or `nullptr` if missing. **Zero lock when frozen.** |
| `get_shared<T>(name = "")` | `std::shared_ptr<T>` | Returns `shared_ptr<T>`, or `nullptr` if missing. **Zero lock when frozen.** |
| `require<T>(name = "")` | `T&` | Returns reference to `T`; throws `std::runtime_error` if missing. |

---

## Example: Clean Multi-Database Architecture

```cpp
int main() {
    Server server;

    // Register primary writer and read replica
    server.provide<SqlDatabaseClient>("primary", std::make_shared<SqlDatabaseClient>(primary_cfg));
    server.provide<SqlDatabaseClient>("replica", std::make_shared<SqlDatabaseClient>(replica_cfg));

    // Register domain repository
    server.provide<UserRepository>();

    // Route: read from replica
    server.router().get("/users/:id", [](Context& ctx) -> Task<void> {
        auto& db = ctx.service<SqlDatabaseClient>("replica");
        // query from replica...
        ctx.res().text("User read from replica");
        co_return;
    });

    // Route: write to primary
    server.router().post("/users", [](Context& ctx) -> Task<void> {
        auto& db = ctx.service<SqlDatabaseClient>("primary");
        // insert into primary...
        ctx.res().status(StatusCode::Created).text("User written to primary");
        co_return;
    });

    // server.run() automatically freezes services for zero-lock hot-path lookups
    server.listen(8080).run();
    return 0;
}
```
