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

## Reading Cookies (`req.cookie()`)

Inbound HTTP cookies sent by the client via the `Cookie` header can be retrieved directly using `ctx.req().cookie(name)`.

Cookie lookups are zero-copy and zero-allocation, returning an `std::optional<std::string_view>` directly referencing the socket buffer:

```cpp
server.router().get("/dashboard", [](Context& ctx) -> Task<void> {
    std::optional<std::string_view> session_id = ctx.req().cookie("session_id");

    if (!session_id) {
        ctx.res().status(StatusCode::Unauthorized).text("Missing session cookie");
        co_return;
    }

    // Zero-allocation access to cookie value
    ctx.res().text("Authenticated with session: " + std::string(*session_id));
    co_return;
});
```

Key features:
- **Zero-Allocation**: No dynamic memory allocations during parsing.
- **RFC 6265 Compliance**: Strips whitespace around delimiters and strips surrounding double-quotes if values are quoted (e.g., `Cookie: token="abc"` produces `abc`).
- **Missing Cookie Safety**: Returns `std::nullopt` if the cookie name or the `Cookie` header is absent.

---

## Form Parameters (`application/x-www-form-urlencoded`)

Inbound HTML form submissions sent via standard `POST` or `PUT` with `Content-Type: application/x-www-form-urlencoded` can be read using `req.form(key)`:

```cpp
server.router().post("/login", [](Context& ctx) -> Task<void> {
    std::optional<std::string> user = ctx.req().form("username");
    std::optional<std::string> pass = ctx.req().form("password");

    if (!user || !pass) {
        ctx.res().status(StatusCode::BadRequest).text("Missing credentials");
        co_return;
    }

    ctx.res().text("Welcome back, " + *user);
    co_return;
});
```

Key features:
* **Automatic URL-Decoding**: Decodes percent-encoded characters (`%20`, `+`, `%21`, etc.) into valid UTF-8 strings.
* **DTO Binding**: For binding entire forms directly into validated C++ structs, see [`ctx.bind_form<T>()`](/guide/context#4-form-body-binding-bind_form).

---

## Multipart Uploads (`multipart/form-data`)

For forms containing file attachments or mixed text/binary fields (RFC 7578), use `req.multipart()`:

```cpp
server.router().post("/upload", [](Context& ctx) -> Task<void> {
    auto form = ctx.req().multipart();
    if (!form) {
        ctx.res().status(StatusCode::BadRequest).text("Malformed multipart payload");
        co_return;
    }

    // 1. Access textual fields
    std::optional<std::string_view> desc = form->get("description");

    // 2. Access uploaded file(s)
    if (auto avatar = form->file("avatar")) {
        std::cout << "Received: " << avatar->filename << " (" << avatar->size() << " bytes)\n";
        std::cout << "MIME type: " << avatar->content_type << "\n";

        // Persist to disk
        avatar->save_to("/var/uploads/" + std::string(avatar->filename));
    }

    ctx.res().status(StatusCode::Created).json(R"({"status":"saved"})");
    co_return;
});
```

### The `FormFile` Object
Each uploaded file in `multipart/form-data` is represented as a zero-copy [`FormFile`](/guide/request):
* `file.name` (`std::string_view`): Field name in the form (e.g. `"avatar"`).
* `file.filename` (`std::string_view`): Original client filename (e.g. `"profile.png"`).
* `file.content_type` (`std::string_view`): Detected or client-provided MIME type (e.g. `"image/png"`).
* `file.data` (`std::string_view`): **Zero-copy** view into the connection's receive buffer.
* `file.size()`: File length in bytes.
* `file.save_to(filepath)`: Saves the binary file directly to disk.

---

## HTTP Protocol & RFC Compliance

The `Request` object exposes helper methods for HTTP/1.1 and HTTP/2 protocol negotiation:

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `req.version()` | `HttpVersion` | Returns `Http1_0`, `Http1_1`, `Http2`, or `Http3`. |
| `req.expect_continue()` | `bool` | Returns `true` if the client sent `Expect: 100-continue`. |
| `req.is_upgrade_h2c()` | `bool` | Returns `true` if the client requested an `Upgrade: h2c` (HTTP/2 cleartext upgrade). |
| `req.headers().empty()` | `bool` | Returns `true` if no headers were parsed. |
