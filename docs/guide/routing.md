# Routing & Groups

Aegon features a two-tier high-performance router built specifically for modern async microservices and REST APIs.

It combines an **$O(1)$ static route hash map** (zero heap allocation lookup) with an optimized **Radix Tree** for parameterized dynamic routes.

---

## Handler Signatures

Every route handler in Aegon accepts a reference to `aegon::http::Context&`. Handlers can be either asynchronous (returning `core::Task<void>`) or synchronous (returning `void`):

### 1. Asynchronous Coroutine Handler (Recommended)

When performing any asynchronous operations (such as SQL queries, Redis calls, or timers), handlers return `aegon::core::Task<void>` and use `co_await` / `co_return`:

```cpp
server.router().get("/users/:id", [](Context& ctx) -> Task<void> {
    auto user_id = ctx.req().param("id");
    // Perform async I/O without blocking the thread
    // auto user = co_await db.find_by_id<User>(user_id);
    ctx.res().text("User ID: " + std::string(user_id.value_or("unknown")));
    co_return;
});
```

### 2. Synchronous Handler

If a route only computes synchronous in-memory logic, Aegon automatically lifts `void(Context&)` functions into coroutines:

```cpp
server.router().get("/health", [](Context& ctx) {
    ctx.res().json(R"({"status":"healthy"})");
});
```

---

## Registering HTTP Routes

The `Router` class provides fluent helpers for all standard HTTP methods:

```cpp
Router& router = server.router();

// GET
router.get("/items", [](Context& ctx) -> Task<void> {
    ctx.res().text("Listing items");
    co_return;
});

// POST
router.post("/items", [](Context& ctx) -> Task<void> {
    ctx.res().status(StatusCode::Created).text("Item created");
    co_return;
});

// PUT
router.put("/items/:id", [](Context& ctx) -> Task<void> {
    ctx.res().text("Item updated");
    co_return;
});

// PATCH
router.patch("/items/:id", [](Context& ctx) -> Task<void> {
    ctx.res().text("Item partially updated");
    co_return;
});

// DELETE
router.del("/items/:id", [](Context& ctx) -> Task<void> {
    ctx.res().status(StatusCode::NoContent);
    co_return;
});
```

You can also register any HTTP method explicitly using `add_route`:

```cpp
router.add_route(Method::OPTIONS, "/cors", [](Context& ctx) -> Task<void> {
    ctx.res().header("Access-Control-Allow-Origin", "*");
    co_return;
});
```

---

## Path Parameters

Routes support dynamic path segment parameters prefixed with a colon (`:param_name`):

```cpp
router.get("/users/:id", [](Context& ctx) -> Task<void> {
    std::optional<std::string_view> id = ctx.req().param("id");
    if (!id) {
        ctx.res().status(StatusCode::BadRequest).text("Missing ID");
        co_return;
    }
    ctx.res().text("Requested user: " + std::string(*id));
    co_return;
});
```

### Multiple Parameters

You can combine multiple parameters across different segments:

```cpp
router.get("/orgs/:org/repos/:repo/issues/:number", [](Context& ctx) -> Task<void> {
    auto org = ctx.req().param("org").value_or("");
    auto repo = ctx.req().param("repo").value_or("");
    auto number = ctx.req().param("number").value_or("");

    ctx.res().text(std::format("Issue #{}: {}/{}", number, org, repo));
    co_return;
});
```

### Wildcard Parameters

For catch-all paths and file hierarchies, routes support wildcard parameters prefixed with an asterisk (`*name`). The wildcard captures the entire remainder of the path:

```cpp
router.get("/assets/*filepath", [](Context& ctx) {
    auto filepath = ctx.req().param("filepath").value_or("");
    ctx.res().text("Serving asset: " + std::string(filepath));
});
```

---

## Static File Serving (`static_files`)

Aegon includes a built-in, production-grade static file server with disk-following in-memory caching and sidecar precompression negotiation.

### Basic Usage

Mount a local filesystem directory under a URL prefix on the `Router` or any `RouteGroup`:

```cpp
// Mount /data/static directory under /static
server.router().static_files("/static", "/data/static");

// On a RouteGroup: serves /api/v1/docs/* from /var/www/docs
auto api = server.router().group("/api/v1");
api.static_files("/docs", "/var/www/docs");
```

### `StaticFilesOptions`

You can customize static file handling by passing a `StaticFilesOptions` struct:

```cpp
server.router().static_files("/assets", "/var/www/assets", StaticFilesOptions{
    .precompressed = true,          // Negotiate .br and .gz sidecars (default: true)
    .cache_in_memory = true,        // RAM cache with mtime revalidation (default: true)
    .index_file = "index.html"      // Directory index fallback (default: "index.html")
});
```

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `precompressed` | `bool` | `true` | When `true`, checks `Accept-Encoding` header and prioritizes precompressed `.br` (Brotli) and `.gz` (Gzip) sidecars if present on disk. |
| `cache_in_memory` | `bool` | `true` | Caches file content in RAM with nanosecond `st_mtime` validation. Modifying or replacing a file on disk immediately invalidates the cache and serves fresh content on the very next request. |
| `index_file` | `std::string` | `"index.html"` | Default file served when a client requests the base directory prefix or a folder. |

### Security & Path Traversal Protection

Aegon strictly protects against directory traversal attacks:
- Any requested path containing `..`, `\`, or null bytes is immediately rejected with `404 Not Found`.
- Leading slashes and empty segments are normalized before resolving target disk paths.

---

## Route Groups & API Versioning

`RouteGroup` allows you to group related endpoints under a common URL prefix. You can create groups and nest sub-groups recursively:

```cpp
// Create an /api group
auto api = server.router().group("/api");

// Nest a /v1 sub-group -> /api/v1
auto v1 = api.group("/v1");

v1.get("/users", [](Context& ctx) -> Task<void> {
    ctx.res().text("API v1: Users");
    co_return;
});

v1.post("/users", [](Context& ctx) -> Task<void> {
    ctx.res().status(StatusCode::Created).text("API v1: User Created");
    co_return;
});

// Nest a /v2 sub-group -> /api/v2
auto v2 = api.group("/v2");

v2.get("/users", [](Context& ctx) -> Task<void> {
    ctx.res().text("API v2: Users (with enhanced schema)");
    co_return;
});
```

### RouteGroup Methods

`RouteGroup` mirrors the `Router` HTTP registration API:

- `group.get(path, handler)`
- `group.post(path, handler)`
- `group.put(path, handler)`
- `group.del(path, handler)`
- `group.patch(path, handler)`
- `group.static_files(path, directory, options)`
- `group.group(sub_prefix)`
- `group.prefix()` — Returns the cumulative path prefix string view.

Path normalization automatically handles leading and trailing slashes so `join_paths("/api/", "/v1")` safely becomes `"/api/v1"`.

---

## Router Architecture & Matching Internals

Aegon's `Router` uses a two-tier hybrid architecture to ensure optimal route dispatch latency:

```
Inbound HTTP Request
         │
         ▼
[ Tier 1: Static Route Map ]
  • Normalized path O(1) hash lookup
  • Zero allocations, instant method match
         │ (miss)
         ▼
[ Tier 2: Radix Tree ]
  • Dynamic token extraction (:id, wildcards)
  • Parameter extraction into Request params vector
         │
         ├── Found handler ─────────► Execute Handler in Exception Boundary
         ├── Path exists, wrong verb ► 405 Method Not Allowed
         └── No route matches ──────► 404 Not Found
```

1. **Tier 1 (Static Fast-Path)**: Purely static routes (e.g. `/`, `/ping`, `/api/v1/metrics`) are indexed in a contiguous array keyed by HTTP verb. Lookups execute in $O(1)$ time with zero memory allocations.
2. **Tier 2 (Radix Tree Dynamic Path)**: Parameterized routes (`:param`) are evaluated via a radix tree that extracts parameter views directly into the request object without copying strings.
3. **HTTP 405 vs 404 Resolution**: If a path exists for `POST` but the client issued `GET`, Aegon recognizes the path match and immediately yields `405 Method Not Allowed` rather than an incorrect `404 Not Found`.

---

## SIMD Vectorized Path Matching (`SimdRouter`)

Header file: `<aegon/http/SimdRouter.hpp>`

For extreme URL parsing throughput, Aegon includes `SimdRouter`, a set of hardware-accelerated string scanning primitives utilizing AVX2 or ARM NEON vector instructions:

| Method | Signature | Description |
|---|---|---|
| `simd_common_prefix()` | `static size_t simd_common_prefix(const char* a, const char* b, size_t max_len)` | Computes longest common prefix using 128/256-bit SIMD vector equality. |
| `simd_find_char()` | `static const char* simd_find_char(const char* s, size_t len, char target)` | Scans a buffer for a delimiter (e.g. `/`, `?`, `:`) using vectorized broadcast registers. |
| `simd_starts_with()` | `static bool simd_starts_with(std::string_view str, std::string_view prefix)` | Accelerated prefix check with unaligned SIMD loads. |

```cpp
#include <aegon/http/SimdRouter.h>

using namespace aegon::http;

// Vectorized delimiter search across URL path
const char* slash = SimdRouter::simd_find_char(path.data(), path.size(), '/');

// Accelerated prefix comparison for router trie nodes
bool matches = SimdRouter::simd_starts_with("/api/v1/users/42", "/api/v1/");
```

