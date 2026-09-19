# CORS Middleware

Aegon provides a built-in, zero-allocation **CORS (Cross-Origin Resource Sharing)** middleware compliant with the W3C Fetch specification and enhanced with enterprise-grade origin validation, subdomain globbing, and private network controls.

```cpp
#include "http/middleware/Cors.h"
using namespace aegon::http::middleware;
```

---

## Quick Start

### 1. Permissive (Public APIs)

For public APIs allowing requests from any origin:

```cpp
Server app;

// Allow any origin, common verbs, and any header
app.use(cors()); 

app.router().get("/public/items", [](Context& ctx) {
    ctx.res().json(R"([{"id":1},{"id":2}])");
});
```

### 2. Strict / Authenticated (Cookies & JWTs)

For protected applications requiring user credentials, specify allowed origins and enable credentials:

```cpp
app.use(cors(CorsConfig::strict({
    "https://dashboard.example.com",
    "https://admin.example.com"
})));
```

---

## Configuration Reference (`CorsConfig`)

Aegon uses modern C++20/C++26 designated initializers for clear, declarative configuration with sensible defaults:

```cpp
struct CorsConfig {
    std::vector<std::string> allowed_origins{};
    std::vector<std::string> origin_patterns{};
    std::function<bool(std::string_view)> origin_validator{nullptr};
    std::vector<Method> allowed_methods{
        Method::GET, Method::POST, Method::PUT, Method::DELETE,
        Method::PATCH, Method::OPTIONS, Method::HEAD
    };
    std::vector<std::string> allowed_headers{"*"};
    std::vector<std::string> expose_headers{};
    bool allow_credentials{false};
    bool allow_private_network{false};
    std::chrono::seconds max_age{86400};
    bool vary_header{true};
};
```

### Options Breakdown

| Field | Type | Default | Description |
|---|---|---|---|
| `allowed_origins` | `std::vector<std::string>` | `{}` | List of exact allowed origins (e.g. `{"https://app.com"}`). Use `{"*"}` for public APIs. |
| `origin_patterns` | `std::vector<std::string>` | `{}` | Wildcard glob patterns for subdomains and ports (e.g. `"https://*.myapp.com"`, `"http://localhost:*"`). |
| `origin_validator` | `std::function<bool(std::string_view)>` | `nullptr` | Dynamic lambda for runtime origin validation (multi-tenant database lookup). |
| `allowed_methods` | `std::vector<Method>` | All common verbs | HTTP verbs permitted during preflight (`Access-Control-Allow-Methods`). |
| `allowed_headers` | `std::vector<std::string>` | `{"*"}` | Permitted request headers (`Access-Control-Allow-Headers`). |
| `expose_headers` | `std::vector<std::string>` | `{}` | Response headers exposed to browser JavaScript (`Access-Control-Expose-Headers`). |
| `allow_credentials` | `bool` | `false` | Enables cookies / Authorization headers (`Access-Control-Allow-Credentials: true`). |
| `allow_private_network`| `bool` | `false` | Supports W3C Private Network Access (PNA) for public-to-private requests. |
| `max_age` | `std::chrono::seconds` | `86400s` (24h) | Preflight cache lifetime (`Access-Control-Max-Age`). |
| `vary_header` | `bool` | `true` | Emits `Vary: Origin` to protect downstream CDNs and reverse proxies. |

---

## Advanced Enterprise Features

### 1. Subdomain Pattern Matching (`origin_patterns`)

Modern cloud architectures deploy frontends across dynamic subdomains or ephemeral staging environments. Aegon provides native, high-performance wildcard globbing:

```cpp
app.use(cors(CorsConfig{
    .origin_patterns = {
        "https://*.company.com",          // Any subdomain (app.company.com, billing.company.com)
        "https://pr-*.preview.dev.io",     // Ephemeral PR environments
        "http://localhost:*"              // Any local development port
    },
    .allow_credentials = true
}));
```

> [!NOTE]
> Pattern matching verifies the full scheme and domain structure. For example, `https://*.company.com` will **reject** unauthorized spoofing attempts such as `https://company.com.attacker.com`.

### 2. Multi-Tenant Dynamic Validator (`origin_validator`)

For multi-tenant SaaS platforms where clients register custom domains at runtime, provide a lambda validator:

```cpp
app.use(cors(CorsConfig{
    .origin_validator = [](std::string_view origin) -> bool {
        // Look up registered tenant domain in Redis cache or memory map
        return tenant_registry.has_domain(origin);
    },
    .allow_credentials = true
}));
```

### 3. W3C Private Network Access (PNA / RFC 1918)

Chrome enforces Private Network Access when a public website (e.g. `https://cloud.myapp.com`) communicates with a service running on `localhost` or an internal corporate subnet (`192.168.x.x`):

```cpp
app.use(cors(CorsConfig{
    .allowed_origins = {"https://cloud.myapp.com"},
    .allow_private_network = true // Sets Access-Control-Allow-Private-Network: true
}));
```

### 4. Exposed Headers for Frontend Clients

By default, browser `fetch()` and `axios` can only inspect safe-list headers (`Content-Type`, `Cache-Control`). To let client-side scripts read tracing or pagination headers:

```cpp
app.use(cors(CorsConfig{
    .expose_headers = {
        "X-Request-ID",
        "X-Total-Count",
        "Content-Disposition"
    }
}));
```

---

## Preflight `OPTIONS` Short-Circuiting

When a browser executes a non-simple cross-origin request (e.g., `POST` with `Content-Type: application/json` or custom headers), it automatically issues an HTTP `OPTIONS` preflight request.

Aegon handles this transparently:
1. Detects `OPTIONS` containing `Access-Control-Request-Method`.
2. Validates the requesting `Origin`.
3. Sets preflight response headers (`Access-Control-Allow-Origin`, `Access-Control-Allow-Methods`, `Access-Control-Allow-Headers`, `Access-Control-Max-Age`).
4. Emits `204 No Content` and **short-circuits immediately**.

```
Browser Preflight (OPTIONS /api/v1/orders)
     │
     ▼
[Aegon Global CORS Middleware]
     │
     ├── Origin allowed? ──► Set 204 No Content + Access-Control-* headers
     │
     └── SHORT-CIRCUIT ──► Return to browser! (Backend /api/v1/orders handler is NOT executed)
```

Developers **never need to manually create dummy `OPTIONS` routes**.

---

## Strict Credentials Rule

Under the W3C Fetch specification:
> When `allow_credentials` is `true`, `Access-Control-Allow-Origin` **must not be `*`**.

Aegon automatically enforces this:
- If `allow_credentials = true`, Aegon reflects the verified, specific requesting origin (e.g. `https://app.com`).
- Automatically emits `Vary: Origin` so CDNs (Cloudflare, Fastly, AWS CloudFront) never serve cached responses from one authenticated user's origin to another.

---

## Built-In Presets

### `CorsConfig::permissive()`
- `allowed_origins = {"*"}`
- `allowed_headers = {"*"}`
- `allowed_methods = {GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD}`
- `allow_credentials = false`
- `max_age = 24h`

### `CorsConfig::strict(origins, credentials = true)`
- `allowed_origins = origins`
- `allow_credentials = credentials`
- `allowed_headers = {"Authorization", "Content-Type", "X-Requested-With", "Accept", "Origin", "X-Request-ID"}`
- `expose_headers = {"X-Request-ID", "Content-Length"}`
- `max_age = 24h`
