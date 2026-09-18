# HTTP Request

Inside every route handler, the incoming HTTP request is accessible via `ctx.req()`. 

The `Request` object is designed for maximum efficiency: it provides zero-copy string views (`std::string_view`) into the underlying socket receive buffer, avoiding unnecessary heap allocations.

---

## Basic Request Properties

```cpp
server.router().post("/api/echo", [](Context& ctx) -> Task<void> {
    const Request& req = ctx.req();

    // HTTP Method (enum class Method)
    Method m = req.method(); // Method::POST
    std::string_view method_str = to_string(m); // "POST"

    // URL Path
    std::string_view path = req.path(); // "/api/echo"

    // Raw Query String (without leading '?')
    std::string_view raw_query = req.query(); // e.g. "page=1&sort=desc"

    // Raw Request Body
    std::string_view body = req.body();

    // HTTP Protocol Version
    HttpVersion ver = req.version();
    std::string_view ver_str = to_string(ver); // "HTTP/1.1", "HTTP/2.0", etc.

    ctx.res().text("Received " + std::string(method_str) + " on " + std::string(path));
    co_return;
});
```

---

## Route Parameters (`:param`)

Parameters captured from dynamic path segments (e.g. `/users/:id`) are retrieved using `req.param(name)`:

```cpp
server.router().get("/users/:id", [](Context& ctx) -> Task<void> {
    std::optional<std::string_view> id = ctx.req().param("id");

    if (!id) {
        ctx.res().status(StatusCode::BadRequest).text("Missing user ID parameter");
        co_return;
    }

    ctx.res().text("User ID: " + std::string(*id));
    co_return;
});
```

Up to 8 route parameters per request are stored inline within a fixed-size stack buffer with zero heap allocations.

---

## Query Parameters (`?key=value`)

Individual query parameters from the URL query string can be parsed on-demand:

```cpp
server.router().get("/search", [](Context& ctx) -> Task<void> {
    std::optional<std::string_view> query = ctx.req().query_param("q");
    std::optional<std::string_view> page  = ctx.req().query_param("page");

    std::string response = "Search query: " + std::string(query.value_or("all"));
    if (page) {
        response += ", Page: " + std::string(*page);
    }

    ctx.res().text(response);
    co_return;
});
```

> [!TIP]
> `req.query("key")` is an alias for `req.query_param("key")`. For binding complex query parameters directly to C++ structs, see [Context & Binding](/guide/context).

---

## Reading Headers & `HeaderMap`

Headers are stored in `HeaderMap`, a small-vector structure holding up to 32 headers inline without heap allocations. Header lookups are strictly **case-insensitive** (per RFC 9110).

### Lookups

```cpp
// Direct header lookup
std::optional<std::string_view> auth = ctx.req().header("Authorization");
std::optional<std::string_view> content_type = ctx.req().header("content-type"); // Case-insensitive!

if (auth && auth->starts_with("Bearer ")) {
    std::string_view token = auth->substr(7);
    // Authenticate token...
}
```

### Checking Existence & Iteration

```cpp
const HeaderMap& headers = ctx.req().headers();

// Check existence
if (headers.contains("X-API-Key")) {
    // ...
}

// Iterate all headers
for (const auto& entry : headers) {
    // entry.name  (std::string_view)
    // entry.value (std::string_view)
}

// Header count
size_t count = headers.size();
```

---

## HTTP Protocol & RFC Compliance

The `Request` object exposes helper methods for HTTP/1.1 and HTTP/2 protocol negotiation:

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `req.version()` | `HttpVersion` | Returns `Http1_0`, `Http1_1`, `Http2`, or `Http3`. |
| `req.expect_continue()` | `bool` | Returns `true` if the client sent `Expect: 100-continue`. |
| `req.is_upgrade_h2c()` | `bool` | Returns `true` if the client requested an `Upgrade: h2c` (HTTP/2 cleartext upgrade). |
| `req.headers().empty()` | `bool` | Returns `true` if no headers were parsed. |
