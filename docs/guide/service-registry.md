# Service Registry & Dependency Injection

Modern web services depend on shared components: database connections, Redis clusters, background job queues, external payment APIs, and telemetry collectors.

Aegon provides a built-in, type-indexed dependency injection container called **`ServiceRegistry`**. It allows you to bind services by type at startup and resolve them safely inside coroutine handlers without global state or singletons.

---

## How It Works

`ServiceRegistry` indexes dependencies by their C++ `std::type_index`. It is synchronized using `std::shared_mutex` (reader-writer lock), ensuring concurrent, non-blocking reads across all thread-per-core event loops.

```
       [ Server Startup ]
               │
               ▼
   server.provide<SqlDatabaseClient>(...)
   server.provide<PerCoreRedisClient>(...)
   server.provide<PaymentGateway>(...)
               │
               ▼
     [ ServiceRegistry ]
   std::type_index ──► std::shared_ptr<void>
               │
               ▼
      [ In Route Handler ]
   auto& db = ctx.service<SqlDatabaseClient>();
```

---

## Registering Services

Services are typically registered on the `Server` during application bootstrapping:

### In-Place Construction (`provide<T, Args...>`)

```cpp
// Constructs std::make_shared<OrderService>(apiKey, timeoutMs)
server.provide<OrderService>("api_key_secret", 5000);
```

### Shared Pointer Registration (`provide<T>`)

```cpp
// Register PerCoreRedisClient for zero-contention thread-affinity Redis
auto redis = std::make_shared<PerCoreRedisClient>(RedisNodeConfig{.host = "127.0.0.1", .port = 6379});
server.provide<PerCoreRedisClient>(redis);
```

---

## Accessing Services in Handlers

Inside any route handler, access services through `ctx`:

### 1. Required Service (`ctx.service<T>()`)

Retrieves a direct reference `T&`. If the service was not registered at startup, it throws a descriptive `std::runtime_error`:

```cpp
server.router().post("/orders", [](Context& ctx) -> Task<void> {
    // Throws std::runtime_error if OrderService is missing
    auto& orders = ctx.service<OrderService>();

    // Process order...
    ctx.res().status(StatusCode::Created).text("Order placed");
    co_return;
});
```

### 2. Optional Service (`ctx.try_service<T>()`)

Returns a pointer `T*`, or `nullptr` if the service is not present:

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

### 3. Check Existence (`ctx.has_service<T>()`)

```cpp
if (ctx.has_service<NotificationService>()) {
    // ...
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
| `register_service<T>(shared_ptr<T>)` | `void` | Registers an instance of type `T`. Overwrites any previous registration of `T`. |
| `has<T>()` | `bool` | Returns `true` if a service of type `T` is registered. |
| `get<T>()` | `T*` | Returns raw pointer to `T`, or `nullptr` if not registered. |
| `get_shared<T>()` | `std::shared_ptr<T>` | Returns `shared_ptr<T>`, or `nullptr` if not registered. |
| `require<T>()` | `T&` | Returns reference to `T`; throws `std::runtime_error` if missing. |

---

## Example: Building a Clean Service Architecture

Here is a complete pattern separating domain services from transport routes:

```cpp
// 1. Define domain service
class UserRepository {
public:
    Task<std::optional<User>> find_by_id(uint64_t id);
    Task<void> save(const User& user);
};

// 2. Bootstrap application
int main() {
    Server server;

    // Register repository
    server.provide<UserRepository>();

    // 3. Consume cleanly in routes
    server.router().get("/users/:id", [](Context& ctx) -> Task<void> {
        auto& repo = ctx.service<UserRepository>();
        auto id = ctx.req().param("id");

        // auto user = co_await repo.find_by_id(parse_id(id));
        ctx.res().text("User fetched");
        co_return;
    });

    server.listen(8080).run();
    return 0;
}
```
