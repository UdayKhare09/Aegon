# Context & Data Binding

The `Context` object (`aegon::http::Context&`) is passed to every route handler. It represents the lifecycle of a single HTTP request-response exchange and unifies:

1. **Inbound Data**: Accessing `ctx.req()`.
2. **Outbound Data**: Building `ctx.res()`.
3. **Data Binding**: Zero-boilerplate deserialization & validation of JSON bodies, query strings, and path parameters.
4. **Service Resolution**: Injecting databases, caches, and business services via the `ServiceRegistry`.
5. **RFC 7807 Error Reporting**: Standardized API error responses via `ctx.problem()`.

---

## Accessing Request & Response

```cpp
server.router().get("/ping", [](Context& ctx) -> Task<void> {
    // Request reference
    auto method = ctx.req().method();

    // Response reference
    ctx.res().text("pong");
    co_return;
});
```

---

## Type-Safe Data Binding

Aegon features automated compile-time binding for JSON bodies, URL query parameters, and route parameters. If parsing or validation fails, Aegon automatically populates `ctx.res()` with appropriate HTTP error codes (400 Bad Request or 422 Unprocessable Entity) and returns `std::nullopt`.

### 1. JSON Body Binding (`bind_json<T>`)

Define your Data Transfer Object (DTO) with standard C++ fields:

```cpp
#include <aegon/data/validation/Validator.h>

struct CreateUserRequest {
    std::string name;
    std::string email;
    int age;

    // Optional validation hook:
    void validate(aegon::validation::ValidationRules& v) const {
        v.check("name", name).required().min_len(2).max_len(50);
        v.check("email", email).required().email();
        v.check("age", age).min(18).max(120);
    }
};
```

Bind directly in the handler:

```cpp
server.router().post("/users", [](Context& ctx) -> Task<void> {
    // 1. Attempt deserialization & validation
    auto dto = ctx.bind_json<CreateUserRequest>();
    if (!dto) {
        // Automatic 400 Bad Request or 422 Unprocessable Entity has already been set!
        co_return;
    }

    // 2. Safe, validated DTO access
    std::cout << "Creating user: " << dto->name << " (" << dto->email << ")\n";
    ctx.res().status(StatusCode::Created).json(*dto);
    co_return;
});
```

#### Automatic Error Responses:
- **Malformed JSON Syntax**: Returns `400 Bad Request` with exact character offset error diagnostics from Glaze.
- **Validation Violations**: Returns `422 Unprocessable Entity` with a structured list of failing fields and violation messages.

---

### 2. Query String Binding (`bind_query<T>`)

Extract and type-convert URL query parameters (`?page=1&limit=25&active=true`) into a typed struct:

```cpp
struct PaginationQuery {
    int page{1};
    int limit{20};
    bool active{true};

    void validate(aegon::validation::ValidationRules& v) const {
        v.check("page", page).min(1);
        v.check("limit", limit).min(1).max(100);
    }
};

server.router().get("/items", [](Context& ctx) -> Task<void> {
    auto query = ctx.bind_query<PaginationQuery>();
    if (!query) co_return;

    ctx.res().text(std::format("Page: {}, Limit: {}, Active: {}", 
                               query->page, query->limit, query->active));
    co_return;
});
```

---

### 3. Route Parameter Binding (`bind_path<T>`)

Convert dynamic path segments into typed numerical or string fields:

```cpp
struct UserRouteParams {
    uint64_t id;
};

server.router().get("/users/:id", [](Context& ctx) -> Task<void> {
    auto params = ctx.bind_path<UserRouteParams>();
    if (!params) co_return;

    ctx.res().text(std::format("Fetching user #{}", params->id));
    co_return;
});
```

---

## Dependency Injection & Services

Handlers access shared application dependencies (like databases, Redis clients, or configuration objects) directly through `Context`:

```cpp
server.router().get("/stats", [](Context& ctx) -> Task<void> {
    // Throws std::runtime_error if service was not registered at server startup
    auto& db = ctx.service<SqlDatabaseClient>();

    // Optional lookup (returns nullptr if absent)
    if (auto* redis = ctx.try_service<RedisClient>()) {
        // Use Redis cache...
    }

    ctx.res().text("Stats retrieved");
    co_return;
});
```

| Method | Return Type | Behavior |
| :--- | :--- | :--- |
| `ctx.service<T>()` | `T&` | Retrieves reference to registered service; throws `std::runtime_error` if missing. |
| `ctx.try_service<T>()` | `T*` | Retrieves pointer to service or `nullptr` if unregistered. |
| `ctx.has_service<T>()` | `bool` | Checks if service of type `T` is registered. |

---

## RFC 7807 Problem Details

Aegon natively supports RFC 7807 (`application/problem+json`) for standardized, machine-readable error responses:

```cpp
server.router().get("/orders/:id", [](Context& ctx) -> Task<void> {
    auto id = ctx.req().param("id").value_or("");

    if (id == "999") {
        ctx.problem(
            StatusCode::NotFound,
            "Order Not Found",
            "No order exists with identifier " + std::string(id),
            "https://api.example.com/errors/not-found"
        );
        co_return;
    }

    ctx.res().text("Order found");
    co_return;
});
```

Output:
```json
{
  "type": "https://api.example.com/errors/not-found",
  "title": "Order Not Found",
  "status": 404,
  "detail": "No order exists with identifier 999",
  "instance": "/orders/999"
}
```

---

## Static File Serving Shorthand

`ctx.send_file(path, mime_type)` is a convenient shorthand for `ctx.res().file(path, mime_type)`:

```cpp
server.router().get("/logo.png", [](Context& ctx) -> Task<void> {
    ctx.send_file("public/images/logo.png");
    co_return;
});
```
