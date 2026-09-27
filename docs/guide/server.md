# Server Lifecycle & Engine Configuration

The `Server` class is the top-level orchestrator of an Aegon web application. It encapsulates network listeners, TLS/QUIC contexts, thread-per-core event loops, dependency injection, and asynchronous lifecycle hooks.

---

## Server Initialization & Networking

```cpp
#include <aegon/http/Server.h>

using namespace aegon::http;

Server server;

// Configure bind address and port
server.listen(8080, "0.0.0.0");

// Multi-Port Listening (Plaintext & TLS on the same worker event loops)
server.enable_tls("/etc/ssl/certs/server.crt", "/etc/ssl/private/server.key");
server.listen(8080);      // Port 8080: Plaintext HTTP/1.1
server.listen_tls(8081);  // Port 8081: TLS HTTP/1.1
server.listen_tls(8443);  // Port 8443: TLS HTTP/2 & HTTP/3
```

> [!NOTE]
> `server.listen(port)` and `server.listen_tls(port)` perform immediate validation and will throw `std::runtime_error` if `port == 0` or outside valid port ranges (`1 - 65535`).
> When multiple ports are configured, all listeners share the same worker threads, buffer pools, and `io_uring` event loops with zero cross-port overhead. Plaintext ports completely bypass TLS/QUIC handshakes.

### TLS / HTTPS Configuration (`TlsContext`)

OpenSSL / BoringSSL wrapper for TLS 1.3 encryption and ALPN protocol negotiation (`h2`, `http/1.1`):

```cpp
server.enable_tls("/etc/ssl/certs/server.crt", "/etc/ssl/private/server.key");
```

| Method | Signature | Description |
|---|---|---|
| `TlsContext()` | `TlsContext(std::string cert_path, std::string key_path)` | Loads X.509 certificate chain and private key. |
| `is_valid()` | `bool is_valid() const noexcept` | Returns true if SSL context initialized and keys verified. |
| `raw_ctx()` | `SSL_CTX* raw_ctx() noexcept` | Accesses the native OpenSSL `SSL_CTX*` pointer. |

`enable_tls` verifies file existence and cryptographic format at startup, throwing descriptive configuration errors if keys or certificates are missing or unreadable.

### HTTP/3 over QUIC & Progressive Alt-Svc Ladder

When TLS is enabled, HTTP/3 (over UDP) is automatically initialized on **every TLS listener port** (e.g. `8081`, `8443`) using `SO_REUSEPORT` across all worker threads. You can toggle HTTP/3 explicitly:

```cpp
server.enable_http3(true); // Default is true when TLS is active
```

Aegon automatically advertises protocol upgrades to connecting clients via RFC-compliant progressive `Alt-Svc` response headers:

- **HTTP/1.1 TLS Connections**:
  ```http
  Alt-Svc: h3=":<port>"; ma=86400, h2=":<port>"; ma=86400
  ```
  *(or `h2=":<port>"; ma=86400` if HTTP/3 is disabled).*
- **HTTP/2 TLS Connections**:
  ```http
  Alt-Svc: h3=":<port>"; ma=86400
  ```
- **Plaintext and HTTP/3 Connections**: No `Alt-Svc` header is emitted.

All listener ports are tracked and passed into connection accept loops directly, eliminating `getsockname()` syscall overhead entirely.

### WebSocket (RFC 6455) Routing

Aegon allows binding WebSocket routes directly onto the `Server` instance:

```cpp
// 1. High-throughput echo endpoint
server.ws_echo("/ws/echo");

// 2. Interactive WebSocket handler
server.ws("/ws/chat", [](WebSocket& ws) -> aegon::core::Task<void> {
    ws.on_text([](WebSocket& ws, std::string_view msg) -> aegon::core::Task<void> {
        co_await ws.send_text(msg);
    });
    co_return;
});
```

See the full [WebSocket Guide](/guide/websocket) for details on SIMD acceleration, event callbacks, and binary streaming.

## Startup Misconfiguration Diagnostics

Aegon performs fail-fast validation before binding sockets or spawning worker event loops:

- **Invalid Port Detection**: Invoking `.listen(0)` or configuring an invalid port throws `std::invalid_argument` with `"Server::listen: port cannot be 0"`.
- **TLS Certificate & Key Verification**: Invoking `.enable_tls(cert, key)` validates that both certificate and private key files exist and are readable (`access(R_OK)`). If missing or unreadable, Aegon immediately throws `std::runtime_error` detailing the missing file path.
- **Fail-Fast Socket Binding**: `create_listen_socket` validates binding and socket creation errors immediately with detailed descriptive messages.

---

## Unified Server Configuration (`ServerConfig`)

Aegon centralizes all protocol constraints, kernel parameters, TCP socket options, HTTP/2 & HTTP/3 flow control, flood mitigations, and WebSocket tuning into the `ServerConfig` structure (`<aegon/http/ServerConfig.h>`).

Each `Server` instance maintains an isolated configuration, allowing multiple servers with completely independent limits (e.g. an IoT telemetry endpoint with 1 KB payload limits vs. a media service with 64 MB limits) to run concurrently in the same process without global state collision.

```cpp
#include <aegon/http/Server.h>
#include <aegon/http/ServerConfig.h>

using namespace aegon::http;

ServerConfig cfg;

// 1. Protocol compliance and payload limits
cfg.limits.max_body_size = 32 * 1024 * 1024; // 32 MB -> 413 Payload Too Large
cfg.limits.max_headers_size = 128 * 1024;    // 128 KB -> 431 Header Fields Too Large
cfg.limits.max_uri_length = 16384;           // 16 KB -> 414 URI Too Long

// 2. Linux io_uring & Provided Buffer Pool
cfg.ring_entries = 8192;                     // Submission queue capacity
cfg.buffer_pool_entries = 16384;             // Multishot provided buffer pool count
cfg.buffer_size = 4096;                      // Buffer slice size in bytes

// 3. TCP socket options
cfg.tcp.nodelay = true;                      // TCP_NODELAY (disable Nagle's algorithm)
cfg.tcp.keepalive = true;                    // SO_KEEPALIVE
cfg.tcp.keepidle = 60;                       // Probes after 60s idle
cfg.tcp.keepintvl = 10;                      // 10s between keepalive probes
cfg.tcp.keepcnt = 3;                         // Drop after 3 unanswered probes

// 4. HTTP/2 and HTTP/3 tuning
cfg.h2_max_concurrent_streams = 256;         // Concurrent streams per client
cfg.h2_initial_window_size = 2 * 1024 * 1024;// 2 MB initial flow control window
cfg.rst_burst_limit = 1000;                  // HTTP/2 & HTTP/3 Rapid Reset flood threshold

// 5. WebSocket tuning
cfg.websocket.max_message_size = 16 * 1024 * 1024; // 16 MB -> CloseCode::MessageTooBig (1009)
cfg.websocket.max_frame_size = 16 * 1024 * 1024;   // 16 MB per frame limit
cfg.websocket.require_masked_frames = true;        // RFC 6455 §5.1 masking enforcement
cfg.websocket.auto_ping_interval_sec = 30;         // Heartbeat ping every 30s (0 = disabled)
cfg.websocket.ping_timeout_sec = 10;               // Close after 10s missing pong
cfg.websocket.initial_buffer_capacity = 4096;      // Buffer pre-allocation per session

// 6. Protocol feature toggles
cfg.enable_http3 = true;                     // Enable HTTP/3 over QUIC on TLS listeners

// Apply full config to server
server.config(cfg);
```

### Fluent Configuration API

You can also configure any setting fluently directly on the `Server` instance:

```cpp
server.max_body_size(32 * 1024 * 1024)
      .max_headers_size(128 * 1024)
      .max_uri_length(16384)
      .ring_entries(8192)
      .buffer_pool_entries(16384)
      .buffer_size(4096)
      .tcp_nodelay(true)
      .tcp_keepalive(true, /*idle=*/60, /*intvl=*/10, /*cnt=*/3)
      .h2_max_concurrent_streams(512)
      .h2_initial_window_size(2 * 1024 * 1024)
      .rst_burst_limit(2000)
      .ws_max_message_size(16 * 1024 * 1024)
      .ws_require_masked_frames(true)
      .ws_auto_ping_interval(30)
      .enable_http3(true);
```

### Configuration Reference

| Option | Method / Field | Default | Description |
|---|---|---|---|
| Max Body Size | `.max_body_size(bytes)` / `limits.max_body_size` | `16 MB` | Inbound payload limit across H1, H2, and H3. Violations return `413 Payload Too Large`. |
| Max Headers Size | `.max_headers_size(bytes)` / `limits.max_headers_size` | `64 KB` | Maximum total HTTP header section size. Violations return `431 Request Header Fields Too Large`. |
| Max URI Length | `.max_uri_length(bytes)` / `limits.max_uri_length` | `8 KB` | Maximum request target length. Violations return `414 URI Too Long`. |
| Ring Entries | `.ring_entries(n)` / `ring_entries` | `4096` | Kernel `io_uring` submission queue entries (SQEs) per worker event loop. |
| Buffer Pool Entries | `.buffer_pool_entries(n)` / `buffer_pool_entries` | `8192` | Number of provided buffer slices registered with the kernel buffer pool group. |
| Buffer Size | `.buffer_size(bytes)` / `buffer_size` | `4096` | Size in bytes of each provided buffer slice. |
| TCP NoDelay | `.tcp_nodelay(bool)` / `tcp.nodelay` | `true` | Enables `TCP_NODELAY` to eliminate Nagle's algorithm delay. |
| TCP KeepAlive | `.tcp_keepalive(bool, idle, intvl, cnt)` / `tcp.keepalive` | `true` | Enables `SO_KEEPALIVE` with idle timer (30s), probe interval (10s), and retry count (3). |
| H2 Max Concurrent Streams | `.h2_max_concurrent_streams(n)` / `h2_max_concurrent_streams` | `256` | Maximum number of concurrent active HTTP/2 streams per client connection. |
| H2 Initial Window Size | `.h2_initial_window_size(n)` / `h2_initial_window_size` | `1 MB` | Initial HTTP/2 stream and connection flow-control window size in bytes. |
| Rapid Reset Burst Limit | `.rst_burst_limit(n)` / `rst_burst_limit` | `1000` | Flood mitigation: threshold of rapid stream resets allowed before terminating connection. |
| Enable HTTP/3 | `.enable_http3(bool)` / `enable_http3` | `true` | Enables HTTP/3 (QUIC) over UDP for all TLS listener ports. |
| WS Max Message Size | `.ws_max_message_size(bytes)` / `websocket.max_message_size` | `16 MB` | Maximum allowed WebSocket message size. Violations close with `1009 MessageTooBig`. |
| WS Max Frame Size | `.ws_max_frame_size(bytes)` / `websocket.max_frame_size` | `16 MB` | Maximum single WebSocket frame payload limit. |
| WS Require Masking | `.ws_require_masked_frames(bool)` / `websocket.require_masked_frames` | `true` | Enforces RFC 6455 §5.1 client frame masking. Can be disabled for internal reverse proxies. |
| WS Auto Ping Interval | `.ws_auto_ping_interval(sec)` / `websocket.auto_ping_interval_sec` | `0` | Proactive heartbeat interval in seconds (`0` = disabled). |

---

## Dependency Injection (`provide`)

You can register shared dependencies at server initialization so they become accessible to all route handlers and lifecycle hooks:

```cpp
// 1. Register SqlDatabaseClient backed by a per-core connection pool
auto pg_pool = drivers::create_postgres_pool("host=127.0.0.1 dbname=prod user=postgres password=secret", 4);
auto db = std::make_shared<SqlDatabaseClient>(*pg_pool);
server.provide<SqlDatabaseClient>(db);

// 2. Register PerCoreRedisClient for zero-contention thread-affinity Redis
auto redis = std::make_shared<PerCoreRedisClient>(RedisNodeConfig{.host = "127.0.0.1", .port = 6379});
server.provide<PerCoreRedisClient>(redis);

// 3. Register PerCoreHttpClient for zero-contention thread-affinity HTTP requests
auto http_client = std::make_shared<PerCoreHttpClient>(ClientConfig{.timeout = std::chrono::milliseconds(5000)});
server.provide<PerCoreHttpClient>(http_client);
```

To retrieve a service from the server instance:

```cpp
std::shared_ptr<SqlDatabaseClient> db = server.service<SqlDatabaseClient>();
std::shared_ptr<PerCoreRedisClient> redis = server.service<PerCoreRedisClient>();
std::shared_ptr<PerCoreHttpClient> http_client = server.service<PerCoreHttpClient>();
```

---

## Asynchronous Lifecycle Hooks

Aegon provides coroutine-driven hooks to execute async workflows during startup, background execution, and graceful shutdown.

```
       [ server.run() ]
              │
              ▼
   1. Execute .on_start() hooks (migrations, cache warming)
              │
              ▼
   2. Spawn .spawn_worker() coroutines (stream consumers)
              │
              ▼
   3. Open listener & accept traffic (io_uring event loops)
              │
         [ SIGINT / SIGTERM / server.stop() ]
              │
              ▼
   4. Stop accepting new connections
              │
              ▼
   5. Execute .on_stop() hooks (graceful draining & flush)
              │
              ▼
       [ Shutdown Complete ]
```

### 1. Startup Hooks (`.on_start`)

Executed asynchronously before the socket starts accepting inbound traffic. Ideal for database migrations and cache pre-warming:

```cpp
server.on_start([](Server& s) -> Task<void> {
    auto db = s.service<SqlDatabaseClient>();
    std::cout << "🚀 Running schema migrations...\n";
    // co_await db->execute_migration();

    std::cout << "⚡ Pre-warming cache...\n";
    co_return;
});
```

### 2. Shutdown Hooks (`.on_stop`)

Executed asynchronously during graceful shutdown:

```cpp
server.on_stop([](Server& s) -> Task<void> {
    std::cout << "🛑 Flushing metrics and draining queues...\n";
    // co_await metrics.flush();
    co_return;
});
```

### 3. Background Workers (`.spawn_worker`)

Spawns long-running coroutines directly on the server's `EventLoop`. Useful for Redis Stream listeners, Kafka consumers, or periodic telemetry tasks:

```cpp
server.spawn_worker([](Server& s, EventLoop& loop) -> Task<void> {
    auto redis = s.service<RedisClient>();
    while (loop.is_running()) {
        // Read from Redis stream asynchronously...
        // auto entries = co_await redis->xreadgroup(...);
    }
    co_return;
});
```

---

## Running the Server

### Single-Threaded Mode

Runs on the calling thread's `EventLoop`:

```cpp
server.run();
```

### Thread-per-Core Shared-Nothing Cluster

Aegon uses Linux `SO_REUSEPORT` kernel load-balancing across independent event loops. Each worker thread runs its own isolated `io_uring` instance pinned to a CPU core with zero lock contention:

```cpp
// Run with 8 worker threads
server.run(8);

// Or automatically match hardware threads:
server.run(std::thread::hardware_concurrency());
```

### Graceful Termination

To programmatically stop the server from a signal handler or admin endpoint:

```cpp
server.stop();
```
