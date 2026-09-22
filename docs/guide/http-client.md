# HTTP Client

Aegon includes an asynchronous, high-performance HTTP/1.1 & HTTPS client built directly on Linux `io_uring` and OpenSSL 3. Designed with symmetrical developer ergonomics to Aegon's server API, `HttpClient` integrates seamlessly with C++20 coroutines, compile-time Glaze JSON serialization, persistent keep-alive connection pooling, and fluent request builders.

---

## Overview & Architecture

The client architecture is modular and zero-allocation wherever possible:

- **`HttpClient`**: The central client engine managing thread-safe configuration and persistent socket pools.
- **`RequestBuilder`**: Fluent, chainable builder for composing requests (`.query()`, `.header()`, `.bearer_auth()`, `.json(dto)`, `.send()`).
- **`ConnectionPool`**: Thread-aware pool of open TCP/TLS sockets recycled across consecutive requests using `keep-alive` per `origin`.
- **`TlsClientStream`**: Asynchronous TLS client wrapper using OpenSSL memory BIOs (`BIO_s_mem`) driven by `io_uring` non-blocking ring operations, supporting Server Name Indication (SNI), Application-Layer Protocol Negotiation (ALPN), and system CA validation.
- **`Url`**: High-performance RFC 3986 URL parser extracting scheme, host, port, path, query, and fragment without heap allocations.

```
       +-----------------------------------------------+
       |             aegon::http::client               |
       |  HttpClient::get("https://api.example.com")  |
       |      .bearer_auth("token")                    |
       |      .json(MyPayload{...})                    |
       +-----------------------+-----------------------+
                               |
               co_await RequestBuilder::send()
                               |
                               v
       +-----------------------------------------------+
       |                ConnectionPool                 |
       |  Acquire persistent socket / TLS stream       |
       +-----------------------+-----------------------+
                               |
                               v
       +-----------------------------------------------+
       |             io_uring (Linux Kernel)           |
       |  Zero-copy send / direct recv / event loop    |
       +-----------------------------------------------+
```

---

## Quick Start

### Asynchronous Coroutine Usage (`co_await send()`)

When executing inside an Aegon handler or worker thread running an `EventLoop`, invoking `.send()` suspends the current coroutine until the response is ready without blocking the thread:

```cpp
#include "http/client/HttpClient.h"

using namespace aegon::http;
using namespace aegon::http::client;

struct UserDto {
    uint64_t id{0};
    std::string name{};
    std::string email{};
};

core::Task<void> fetch_user_handler(Context& ctx) {
    HttpClient client;

    // 1. Send GET request with query params
    Response res = co_await client.get("https://jsonplaceholder.typicode.com/users/1")
        .accept("application/json")
        .timeout(std::chrono::milliseconds(5000))
        .send();

    if (res.is_success()) {
        // Deserialize response body into typed DTO using Glaze
        if (auto user = res.json<UserDto>()) {
            ctx.res().status(StatusCode::Ok).json(*user);
            co_return;
        }
    }

    ctx.res().status(StatusCode::BadGateway).text("Upstream service failure");
}
```

### Synchronous Usage (`send_sync()`)

For scripts, CLI utilities, unit tests, or initialization code outside an event loop, `.send_sync()` runs an isolated local event loop on the current thread and returns the `Response` directly:

```cpp
HttpClient client;

Response res = client.get("http://localhost:8080/health")
    .send_sync();

if (res.is_success()) {
    std::cout << "Server is healthy! Body: " << res.body() << "\n";
}
```

---

## Request Building

`RequestBuilder` provides a fluent, ergonomic API for configuring every aspect of an outbound HTTP request:

### HTTP Verb Helpers

```cpp
HttpClient client;

auto req1 = client.get("https://api.example.com/items");
auto req2 = client.post("https://api.example.com/items");
auto req3 = client.put("https://api.example.com/items/42");
auto req4 = client.patch("https://api.example.com/items/42");
auto req5 = client.del("https://api.example.com/items/42");
auto req6 = client.head("https://api.example.com/items/42");
auto req7 = client.options("https://api.example.com/items");
auto req8 = client.request(Method::GET, "https://api.example.com/items");
```

### Query Parameters

Query parameters are automatically URL-encoded according to RFC 3986:

```cpp
Response res = client.get("https://api.example.com/search")
    .query("q", "distributed systems & io_uring")
    .query("page", "1")
    .query("limit", "25")
    .send_sync();
```

You can also pass a vector of pairs:

```cpp
std::vector<std::pair<std::string, std::string>> params = {
    {"filter", "active"},
    {"sort", "created_desc"}
};

auto req = client.get("https://api.example.com/users").query(params);
```

### Headers & Authentication

`RequestBuilder` provides convenience methods for common authentication patterns:

```cpp
Response res = client.get("https://api.example.com/secure")
    // Bearer Token (Authorization: Bearer <token>)
    .bearer_auth("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...")

    // Basic Auth (Authorization: Basic <base64(user:pass)>)
    // .basic_auth("admin", "secretpassword")

    // Custom API Key header
    // .api_key("X-API-Key", "my_api_key_123")

    // Standard headers
    .content_type("application/json")
    .accept("application/json")
    .user_agent("MyApp/2.0")
    .header("X-Custom-Trace-ID", "trace-98765")
    .send_sync();
```

### Cookies

```cpp
Response res = client.get("https://api.example.com/profile")
    .cookie("session_id", "sess_abcdef123456")
    .cookie("theme", "dark")
    .send_sync();
```

### Request Body & Serialization

- **Typed JSON (via Glaze)**:
  ```cpp
  struct CreateUserRequest {
      std::string name;
      std::string email;
  };

  Response res = co_await client.post("https://api.example.com/users")
      .json(CreateUserRequest{"Alice", "alice@example.com"})
      .send();
  ```

- **Raw String or Bytes**:
  ```cpp
  client.post("https://api.example.com/raw")
      .body("raw text payload")
      .send_sync();
  ```

- **Form URL-Encoded**:
  ```cpp
  client.post("https://api.example.com/oauth/token")
      .form({
          {"grant_type", "client_credentials"},
          {"client_id", "my_client_id"}
      })
      .send_sync();
  ```

---

## Response Ergonomics

The `Response` returned by `HttpClient` mirrors the server-side `Response` object:

```cpp
Response res = client.get("https://api.example.com/data").send_sync();

// Status Inspection
uint16_t code = res.status_code();        // e.g. 200
StatusCode st = res.status();             // StatusCode::Ok
bool ok       = res.is_success();         // true if 200 <= code <= 299

// Headers
if (auto ct = res.headers().get("content-type")) {
    std::cout << "Content-Type: " << *ct << "\n";
}

// Body Access
std::string_view raw = res.body();

// HTTP Protocol Version Inspection
HttpVersion ver = res.version();           // e.g. HttpVersion::Http1_1

// Typed JSON Deserialization (Glaze)
if (auto data = res.json<MyDataDto>()) {
    // Process *data
}
```

---

## HTTP Protocol Versions & Negotiation

Aegon provides first-class representations for HTTP protocol versions via the `HttpVersion` enumeration:

```cpp
enum class HttpVersion : uint8_t {
    Http1_0,  // HTTP/1.0 (RFC 1945)
    Http1_1,  // HTTP/1.1 (RFC 9112) - Default
    Http2,    // HTTP/2 (RFC 9113)
    Http3     // HTTP/3 (RFC 9114)
};
```

### Protocol Support Matrix

| Protocol | Client Status | Server Status | Notes |
|---|---|---|---|
| **HTTP/1.1** | **Full Support** | **Full Support** | Default protocol (`HttpVersion::Http1_1`). Full keep-alive socket reuse, Content-Length & chunked transfer encoding (`Transfer-Encoding: chunked`). |
| **HTTP/1.0** | **Full Support** | **Full Support** | Supported via `HttpVersion::Http1_0`. Automatic connection teardown unless explicit `Connection: keep-alive` is exchanged. |
| **HTTP/2 (h2 & h2c)** | **Full Support** | **Full Support** | Native binary framing & multiplexing (RFC 9113, RFC 7541) powered by `nghttp2`. Supports TLS (`h2`) and cleartext prior-knowledge (`h2c`) via `.http2()`. |
| **HTTP/3 (h3)** | **Full Support** | **Full Support** | Native HTTP/3 over QUIC (RFC 9000, RFC 9114, RFC 9204) powered by `ngtcp2`, `nghttp3`, and OpenSSL quictls. Fluent `.http3()` API. |

### Native HTTP/2 Multiplexer (`.http2()`)

Aegon provides native client-side HTTP/2 stream multiplexing (RFC 9113, RFC 7541) powered by `nghttp2`. The client supports both encrypted TLS connections (**h2**) using ALPN negotiation and cleartext prior-knowledge (**h2c**) for high-performance internal microservices.

Requests can be dispatched over HTTP/2 simply by appending `.http2()` or `.version(HttpVersion::Http2)` to any request builder:

#### Synchronous HTTP/2 Call

```cpp
HttpClient client;

// Cleartext h2c or TLS h2
Response res = client.get("http://localhost:8080/api/status")
    .http2()
    .send_sync();

if (res.is_success()) {
    std::cout << "Version: " << (res.version() == HttpVersion::Http2 ? "HTTP/2" : "Other") << "\n";
    std::cout << "Body: " << res.body() << "\n";
}
```

#### Asynchronous HTTP/2 Call with Typed JSON

```cpp
core::Task<void> call_h2_service(HttpClient& client) {
    OrderDto order{/* ... */};

    Response res = co_await client.post("https://api.example.com/v2/orders")
        .http2()
        .bearer_auth("token_xyz")
        .json(order)
        .send();

    if (res.is_success()) {
        auto conf = res.json<OrderConfirmationDto>();
        // Process confirmation
    }
}
```

### Native HTTP/3 over QUIC (`.http3()`)

Aegon features native HTTP/3 client support over UDP and QUIC (RFC 9000, RFC 9114, RFC 9204), using `ngtcp2` for transport and `nghttp3` for stream multiplexing and QPACK header compression.

Requests can be dispatched over HTTP/3 simply by appending `.http3()` or `.version(HttpVersion::Http3)` to any request builder:

#### Synchronous HTTP/3 Call

```cpp
HttpClient client(ClientConfig{
    .tls = TlsClientOptions{
        .insecure_skip_verify = true // For self-signed dev/staging certs
    }
});

Response res = client.get("https://api.example.com/status")
    .http3()
    .send_sync();

if (res.is_success()) {
    std::cout << "Version: " << (res.version() == HttpVersion::Http3 ? "HTTP/3" : "Other") << "\n";
    std::cout << "Body: " << res.body() << "\n";
}
```

#### Asynchronous HTTP/3 Call with Typed JSON

```cpp
core::Task<void> call_h3_api(HttpClient& client) {
    UserDto payload{42, "quic_hero"};

    Response res = co_await client.post("https://api.example.com/v1/users")
        .http3()
        .bearer_auth("token123")
        .json(payload)
        .send();

    if (res.is_success()) {
        auto result = res.json<UserDto>();
        // Process result
    }
}
```

### Configuring the Protocol Version

You can configure the default protocol globally on `ClientConfig` or override it fluently on individual requests:

```cpp
// 1. Global default protocol on HttpClient
HttpClient client(ClientConfig{
    .default_protocol = HttpVersion::Http1_1
});

// 2. Per-request override on RequestBuilder
Response res = client.get("http://localhost:8080/legacy-endpoint")
    .version(HttpVersion::Http1_0)
    .send_sync();
```

### Inspecting Response HTTP Version

The response carries the parsed HTTP protocol version returned by the remote server:

```cpp
Response res = client.get("https://api.example.com/status").send_sync();

switch (res.version()) {
    case HttpVersion::Http1_1:
        std::cout << "Negotiated HTTP/1.1\n";
        break;
    case HttpVersion::Http2:
        std::cout << "Negotiated HTTP/2\n";
        break;
    case HttpVersion::Http3:
        std::cout << "Negotiated HTTP/3\n";
        break;
    case HttpVersion::Http1_0:
        std::cout << "Legacy HTTP/1.0\n";
        break;
}
```

### ALPN (Application-Layer Protocol Negotiation) over TLS

When connecting over HTTPS, `TlsClientStream` utilizes RFC 7301 Application-Layer Protocol Negotiation (ALPN) within the TLS ClientHello handshake to agree on the application protocol without introducing extra round-trips:

- The client advertises acceptable protocols during the TLS ClientHello.
- The negotiated protocol is preserved on the pooled connection:
  ```cpp
  std::string_view negotiated = conn->alpn(); // "http/1.1", "h2", etc.
  bool h2 = conn->is_h2();
  ```

---

## Client Configuration & Connection Pooling

Client configuration is defined via `ClientConfig`:

```cpp
ClientConfig config{
    .timeout = std::chrono::milliseconds(10000),         // Request timeout
    .connect_timeout = std::chrono::milliseconds(3000), // Socket connect timeout
    .max_connections_per_host = 16,                     // Max pooled sockets per origin
    .max_idle_connections = 64,                         // Global max idle sockets
    .idle_timeout = std::chrono::seconds(60),           // Max idle time before socket eviction
    .follow_redirects = true,                           // Auto follow 3xx redirects
    .user_agent = "Aegon-HttpClient/1.0",
    .tls = TlsClientOptions{
        .verify_peer = true,
        .verify_hostname = true,
        .insecure_skip_verify = false
    }
};

HttpClient client(config);
```

### Connection Pool Behavior

1. **Keep-Alive Reuse**: Outbound connections default to `Connection: keep-alive`. When a server replies with keep-alive, the underlying socket is returned to the `ConnectionPool` mapped by origin (`http://host:port` or `https://host:port`).
2. **EventLoop Migration**: Pooled connections transparently update their active `EventLoop` pointer when acquired across different worker threads or coroutine frames.
3. **Idle Eviction**: Connections exceeding `idle_timeout` are automatically closed and cleaned up on subsequent `acquire()` calls.
4. **Explicit Teardown**: Calling `client.close()` shuts down and closes all active and idle sockets in the pool.

---

## HTTPS & TLS Configuration

`HttpClient` supports encrypted TLS 1.2 and 1.3 communication using OpenSSL 3:

```cpp
// 1. Strict Production HTTPS (default)
// Uses system CA certificate store (e.g. /etc/ssl/certs) with strict hostname validation
HttpClient client;

// 2. Custom CA Bundle (e.g. internal corporate PKI or private Kubernetes CA)
HttpClient ca_client(ClientConfig{
    .tls = TlsClientOptions{
        .ca_bundle_path = "/path/to/custom_ca.pem"
    }
});

// 3. Mutual TLS (mTLS) Client Authentication
HttpClient mtls_client(ClientConfig{
    .tls = TlsClientOptions{
        .client_cert_path = "/etc/certs/client.crt",
        .client_key_path = "/etc/certs/client.key"
    }
});

// 4. Insecure Skip Verify (for local testing / self-signed certificates)
HttpClient dev_client(ClientConfig{
    .tls = TlsClientOptions{
        .insecure_skip_verify = true
    }
});
```

---

## Multi-Threaded Servers & `PerCoreHttpClient`

When running Aegon in a multi-threaded server (`server.run(N)`), sharing a single global `HttpClient` across worker threads introduces cross-thread lock contention inside the socket connection pool and violates `io_uring` thread-affinity.

Aegon provides **`PerCoreHttpClient`** (`#include "http/client/PerCoreHttpClient.h"`):

```
                                PerCoreHttpClient
                                        │
                 ┌──────────────────────┼──────────────────────┐
                 ▼                      ▼                      ▼
           Worker Core 0          Worker Core 1          Worker Core 2
                 │                      │                      │
                 ▼                      ▼                      ▼
         Private HttpClient     Private HttpClient     Private HttpClient
                 │                      │                      │
                 ▼                      ▼                      ▼
         Core 0 Sockets         Core 1 Sockets         Core 2 Sockets
                 │                      │                      │
                 ▼                      ▼                      ▼
          Core 0 io_uring        Core 1 io_uring        Core 2 io_uring
```

### Key Advantages:
1. **Zero Cross-Ring I/O**: Sockets are bound to the calling thread's `io_uring` ring, preventing cross-core ring submission or wait penalties.
2. **Lock-Free Connection Pool**: Each CPU core maintains its own isolated keep-alive connection pool. No mutex contention on socket checkout or release.
3. **L1/L2 Cache Locality**: Memory buffers, TLS sessions, and HPACK dynamic tables stay pinned to the worker core's local CPU caches.
4. **Service Registry Integration**: Easily registered in `server.provide<PerCoreHttpClient>()` and accessed directly via `ctx.service<PerCoreHttpClient>()`.

### Registration & Handler Usage:

```cpp
#include "http/Server.h"
#include "http/client/PerCoreHttpClient.h"

int main() {
    Server server;

    // 1. Register PerCoreHttpClient in the Service Registry
    auto http_client = std::make_shared<PerCoreHttpClient>(ClientConfig{
        .timeout = std::chrono::milliseconds(5000),
        .user_agent = "Aegon-Microservice/1.0"
    });
    server.provide<PerCoreHttpClient>(http_client);

    // 2. Access in route handlers with zero lock overhead
    server.router().get("/proxy-user/:id", [](Context& ctx) -> core::Task<void> {
        auto id = ctx.req().param("id").value_or("0");
        auto& client = ctx.service<PerCoreHttpClient>();

        auto res = co_await client.get("https://api.internal/users/" + std::string(id))
            .bearer_auth("internal-secret-token")
            .send();

        ctx.res().status(res.status()).json(res.body());
    });

    server.listen(8080);
    server.run(4); // 4 worker threads, each with its own isolated HttpClient
}
```

