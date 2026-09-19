# Middleware Pipeline

Aegon features a coroutine-native, onion-model middleware architecture designed for zero per-request heap allocation. Middlewares can execute pre-handler logic, delegate down the chain via `co_await next(ctx)`, inspect or mutate the response during the post-handler phase, or terminate the pipeline early (**short-circuit**).

---

## The Onion Execution Model

In Aegon, middleware functions wrap downstream handlers like layers of an onion:

```
Inbound Request
  │
  ▼
[Global Middleware (Pre-handler)]
  │
  ▼
[RouteGroup Middleware (Pre-handler)]
  │
  ▼
[Per-Route Middleware (Pre-handler)]
  │
  ▼
[Target Route Handler (ctx)]
  │
  ▲
[Per-Route Middleware (Post-handler)]
  │
  ▲
[RouteGroup Middleware (Post-handler)]
  │
  ▲
[Global Middleware (Post-handler)]
  │
  ▼
Outbound Response
```

### Core Signatures

Middlewares are defined under `#include "http/Middleware.h"`. The core type aliases are:

```cpp
namespace aegon::http {
    using Next = std::function<core::Task<void>(Context&)>;
    using MiddlewareFn = std::function<core::Task<void>(Context&, Next)>;
}
```

Aegon automatically adapts four callable forms:
1. **Asynchronous Onion**: `Task<void>(Context&, Next)` — full pre/post coroutine execution.
2. **Synchronous with Next**: `void(Context&, Next)`.
3. **Asynchronous Pre-Handler**: `Task<void>(Context&)` — automatically invokes `next(ctx)`.
4. **Synchronous Pre-Handler**: `void(Context&)` — executes before `next(ctx)` without coroutine overhead.

---

## Writing a Middleware

### 1. Basic Onion Middleware

```cpp
#include "http/Server.h"
#include "http/Middleware.h"
#include <chrono>
#include <iostream>

using namespace aegon::http;

auto timing_middleware() -> MiddlewareFn {
    return [](Context& ctx, Next next) -> core::Task<void> {
        auto start = std::chrono::steady_clock::now();

        // 1. Pre-handler logic
        ctx.res().header("X-Framework", "Aegon");

        // 2. Delegate to next middleware or route handler
        co_await next(ctx);

        // 3. Post-handler logic (runs after route handler finishes)
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start
        );
        ctx.res().header("X-Response-Time", std::to_string(elapsed.count()) + "us");
    };
}
```

### 2. Short-Circuiting (Authentication / Guard)

To abort processing and bypass downstream handlers, simply write your response status/body and return without invoking `co_await next(ctx)`:

```cpp
auto require_api_key() -> MiddlewareFn {
    return [](Context& ctx, Next next) -> core::Task<void> {
        auto key = ctx.req().header("X-API-Key");
        if (key != "secret-token-123") {
            // Short-circuit: do NOT call next(ctx)!
            ctx.res().status(StatusCode::Unauthorized).json(R"({"error":"Unauthorized"})");
            co_return;
        }

        // Key is valid -> continue
        co_await next(ctx);
    };
}
```

---

## Attachment Scopes

Middlewares can be attached at three distinct granularities:

### 1. Global Scope (`Server::use` / `Router::use`)

Global middlewares apply to all incoming requests, including preflights and error responses:

```cpp
Server app;

// Applied globally to all requests
app.use(timing_middleware())
   .use(cors());
```

### 2. RouteGroup Scope (`RouteGroup::use`) & Inheritance

Group-scoped middlewares apply only to routes within that group. Nested sub-groups automatically **inherit** the parent group's middleware chain:

```cpp
auto api = app.router().group("/api");
api.use(require_api_key()); // Applies to all /api routes

auto v1 = api.group("/v1"); // Automatically inherits require_api_key()!
v1.use(rate_limiter());      // Applies only to /api/v1 routes

v1.get("/users", [](Context& ctx) {
    // Chain executed: Global -> api -> v1 -> handler
    ctx.res().text("Users List");
});

api.get("/ping", [](Context& ctx) {
    // Chain executed: Global -> api -> handler (v1 middleware does NOT run)
    ctx.res().text("pong");
});
```

### 3. Per-Route Scope

Individual routes can accept an explicit `std::vector<MiddlewareFn>` parameter:

```cpp
std::vector<MiddlewareFn> admin_guards = {
    require_api_key(),
    require_admin_role()
};

// Route-level middleware runs after group middleware, right before handler
api.get("/admin/metrics", admin_guards, [](Context& ctx) {
    ctx.res().json(R"({"metrics":"ok"})");
});
```

---

## Per-Request Typed Data Bag

Middlewares frequently need to pass state (such as decoded JWT claims, user sessions, or tracing IDs) to downstream handlers. Aegon provides a type-safe per-request store on `Context`.

> [!TIP]
> This store is isolated per-request and has zero interaction with the long-lived singleton `ServiceRegistry`.

| Method | Return Type | Description |
|---|---|---|
| `ctx.set<T>(value)` | `void` | Stores a typed value in the request context. |
| `ctx.get<T>()` | `T*` (nullable) | Non-throwing retrieval; returns `nullptr` if not set. |
| `ctx.local<T>()` | `T&` (reference) | Throws `std::runtime_error` if not set. |
| `ctx.has<T>()` | `bool` | Returns `true` if type `T` is present. |

### Example: Setting & Consuming Typed State

```cpp
struct AuthUser {
    std::string user_id;
    std::string role;
};

// Auth middleware sets state
auto auth_middleware() -> MiddlewareFn {
    return [](Context& ctx, Next next) -> core::Task<void> {
        // Authenticate user...
        ctx.set<AuthUser>(AuthUser{
            .user_id = "usr_10482",
            .role = "admin"
        });
        co_await next(ctx);
    };
}

// Handler reads typed state
app.router().get("/dashboard", { auth_middleware() }, [](Context& ctx) {
    // Non-throwing check
    if (auto* user = ctx.get<AuthUser>()) {
        ctx.res().text("Welcome, " + user->user_id);
        return;
    }

    // Or throwing access:
    // AuthUser& user = ctx.local<AuthUser>();
});
```

---

## Error Propagation

Exceptions thrown inside a middleware or route handler propagate naturally through standard C++ coroutine unwinding:

1. Any middleware wrapping `next(ctx)` can catch exceptions using `try / catch`:
   ```cpp
   [](Context& ctx, Next next) -> core::Task<void> {
       try {
           co_await next(ctx);
       } catch (const std::exception& e) {
           std::cerr << "Caught error in middleware: " << e.what() << "\n";
           ctx.res().status(StatusCode::InternalServerError).body("Error");
       }
   };
   ```
2. Uncaught exceptions automatically propagate to the router's `set_error_handler` or emit a standardized RFC 7807 500 JSON response.

---

## Performance Guarantees

1. **Zero Heap Allocation on Dispatch**: The chain runner (`run_chain`) operates over contiguous `std::span` views of pre-registered middleware vectors.
2. **Zero-Overhead Fast Paths**: Routes without group or route middleware store the raw `Handler` directly in the radix tree without lambda wrapper overhead.
3. **Unmodified Radix Tree**: Group and route middleware are pre-composed into the terminal handler at route registration time, ensuring SIMD exact-match lookups remain completely untouched.
