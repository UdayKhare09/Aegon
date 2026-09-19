# Authentication & Role-Based Access Control (RBAC)

Aegon provides a modular, header-only authentication and authorization subsystem. It integrates with Aegon's type-safe `Context` (`ctx.set<T>` and `ctx.get<T>`), enforces RFC 7807 Problem Details error responses, and validates role permissions at compile-time using C++20 concepts.

```cpp
#include "http/middleware/Auth.h"
#include "http/middleware/RbacGuard.h"

using namespace aegon::http::middleware;
```

---

## Authentication Middleware Factories

Aegon includes 4 built-in extractor factories. Each factory handles header/cookie extraction, parses credentials, calls your user-defined asynchronous validator, and registers the resolved principal into `ctx.set<T>(principal)`.

### 1. Bearer Token Authentication (`bearer_auth`)

Extracts tokens from the `Authorization: Bearer <token>` header:

```cpp
struct User {
    uint64_t id;
    std::string email;
    std::vector<std::string> roles;
    std::span<const std::string> get_roles() const { return roles; }
};

app.use(bearer_auth<User>({
    .validator = [&jwt_verifier](std::string_view token, Context& ctx) -> core::Task<std::optional<User>> {
        auto res = jwt_verifier.verify(token);
        if (!res.has_value()) {
            co_return std::nullopt;
        }
        co_return res->claims;
    }
}));
```

### 2. Cookie Authentication (`cookie_auth`)

Extracts session tokens or encrypted cookies from `Cookie: <name>=<value>`:

```cpp
struct Session {
    std::string session_id;
    uint64_t user_id;
};

app.use(cookie_auth<Session>("session_id", {
    .validator = [&db](std::string_view session_id, Context& ctx) -> core::Task<std::optional<Session>> {
        auto session = co_await db.find_session(session_id);
        co_return session;
    }
}));
```

### 3. API Key Authentication (`api_key_auth`)

Extracts API keys from arbitrary request headers (e.g. `X-API-Key`):

```cpp
struct ApiClient {
    std::string key;
    std::string plan;
};

app.use(api_key_auth<ApiClient>("X-API-Key", {
    .validator = [&cache](std::string_view key, Context& ctx) -> core::Task<std::optional<ApiClient>> {
        auto client = co_await cache.lookup_api_key(key);
        co_return client;
    }
}));
```

### 4. HTTP Basic Authentication (`basic_auth`)

Extracts and Base64-decodes `Authorization: Basic <base64(user:pass)>` credentials conforming to RFC 7617:

```cpp
app.use(basic_auth<User>({
    .validator = [&user_service](std::string_view username, std::string_view password, Context& ctx) -> core::Task<std::optional<User>> {
        co_return co_await user_service.verify_credentials(username, password);
    },
    .realm = "Secure Admin Area"
}));
```

---

## Accessing Principal in Handlers

Once an authentication middleware validates a request, the principal is available in any subsequent middleware or route handler via `ctx.get<T>()` or `ctx.local<T>()`:

```cpp
app.router().get("/api/me", [](Context& ctx) {
    const User* user = ctx.get<User>();
    if (!user) {
        ctx.res().status(StatusCode::Unauthorized);
        return;
    }
    ctx.res().json(*user);
});
```

---

## 401 Unauthorized Handling & Options

When authentication fails:
1. HTTP status code is set to `401 Unauthorized`.
2. Standard RFC 7807 Problem Details JSON is returned with `Content-Type: application/problem+json`:
   ```json
   {
     "type": "about:blank",
     "title": "Unauthorized",
     "status": 401,
     "detail": "Authentication credentials are missing or invalid."
   }
   ```
3. Appropriate `WWW-Authenticate` challenge headers are set (e.g. `Bearer` or `Basic realm="..."`).

### Customizing Unauthorized Behavior

Each auth options struct supports:
- `on_unauthorized`: Custom callback to override response headers, redirection, or body.
- `abort_on_failure`: If set to `false`, unauthenticated requests proceed to `next(ctx)` without failing, allowing handlers to inspect `ctx.has<T>()` for optional authentication.

```cpp
app.use(bearer_auth<User>({
    .validator = my_validator,
    .on_unauthorized = [](Context& ctx) {
        ctx.res().status(StatusCode::Unauthorized).json(R"({"error":"custom_auth_failure"})");
    },
    .abort_on_failure = false // Optional auth
}));
```

---

## Role-Based Access Control (RBAC)

Aegon provides route-level authorization guards that enforce user permissions.

### The `RoleHolder` Concept

To prevent runtime errors, the RBAC system uses a C++20 concept:

```cpp
template <typename T>
concept RoleHolder = requires(const T& t) {
    { t.get_roles() } -> std::convertible_to<std::span<const std::string>>;
};
```

Your principal struct must implement `get_roles()` returning `std::span<const std::string>`, `const std::vector<std::string>&`, or `std::vector<std::string>`:

```cpp
struct User {
    uint64_t id;
    std::string email;
    std::vector<std::string> roles;

    std::span<const std::string> get_roles() const { return roles; }
};
```

> [!TIP]
> If a struct without `get_roles()` is passed to `require_role<T>()`, the compiler emits a clear, readable compile-time concept error immediately.

### RBAC Guards

Aegon provides three guard patterns:

#### 1. Single Role Requirement
Requires the principal to have the specified role:

```cpp
app.router().get("/admin/users", { require_role<User>("ADMIN") }, [](Context& ctx) {
    ctx.res().body("User management");
});
```

#### 2. Multiple Roles Requirement (AND Logic)
Requires the principal to possess **all** listed roles:

```cpp
app.router().post("/finance/payout", { 
    require_role<User>({"ADMIN", "FINANCE_OFFICER"}) 
}, payout_handler);
```

#### 3. Any Role Requirement (OR Logic)
Requires the principal to possess **at least one** of the listed roles:

```cpp
app.router().get("/support/queue", { 
    require_any_role<User>({"SUPPORT_TIER_1", "SUPPORT_TIER_2", "ADMIN"}) 
}, support_handler);
```

### 403 Forbidden Responses

When authorization fails (either because the principal is missing from the context or lacks the required roles), the guard short-circuits the pipeline and emits a `403 Forbidden` RFC 7807 response:

```json
{
  "type": "about:blank",
  "title": "Forbidden",
  "status": 403,
  "detail": "Access denied: missing required role 'ADMIN'."
}
```

---

## Complete End-to-End Example

```cpp
#include "http/Server.h"
#include "http/jwt/JwtSigner.h"
#include "http/jwt/JwtVerifier.h"
#include "http/middleware/Auth.h"
#include "http/middleware/RbacGuard.h"

using namespace aegon::http;
using namespace aegon::http::jwt;
using namespace aegon::http::middleware;

struct Account {
    uint64_t id;
    std::string email;
    std::vector<std::string> roles;
    std::span<const std::string> get_roles() const { return roles; }
};

int main() {
    Server app;
    std::string jwt_secret = "production-grade-random-secret-key-32";

    JwtSigner<Account> signer(Algorithm::HS256, jwt_secret);
    JwtVerifier<Account> verifier(Algorithm::HS256, jwt_secret, {
        .issuer = "aegon-service"
    });

    // 1. Public Login Route
    app.router().post("/auth/login", [&signer](Context& ctx) {
        Account acc{101, "admin@corp.io", {"ADMIN", "USER"}};
        std::string token = signer.sign(acc, std::chrono::hours(12));
        ctx.res().json("{\"token\":\"" + token + "\"}");
    });

    // 2. Protected Route Group with Bearer Auth
    auto& api = app.router().group("/api");
    api.use(bearer_auth<Account>({
        .validator = [&verifier](std::string_view token, Context&) -> aegon::core::Task<std::optional<Account>> {
            auto res = verifier.verify(token);
            if (!res.has_value()) co_return std::nullopt;
            co_return res->claims;
        }
    }));

    // Protected User Endpoint
    api.get("/me", [](Context& ctx) {
        ctx.res().json(*ctx.get<Account>());
    });

    // Protected Admin-Only Endpoint
    api.get("/admin/metrics", { require_role<Account>("ADMIN") }, [](Context& ctx) {
        ctx.res().json("{\"system\":\"optimal\"}");
    });

    app.listen(8080);
    return 0;
}
```
