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

### HTTP/3 over QUIC

When TLS is enabled, HTTP/3 (over UDP) is enabled by default. You can toggle HTTP/3 explicitly:

```cpp
server.enable_http3(true); // Default is true when TLS is active
```

---

## Startup Misconfiguration Diagnostics

Aegon performs fail-fast validation before binding sockets or spawning worker event loops:

- **Invalid Port Detection**: Invoking `.listen(0)` or configuring an invalid port throws `std::invalid_argument` with `"Server::listen: port cannot be 0"`.
- **TLS Certificate & Key Verification**: Invoking `.enable_tls(cert, key)` validates that both certificate and private key files exist and are readable (`access(R_OK)`). If missing or unreadable, Aegon immediately throws `std::runtime_error` detailing the missing file path.
- **Fail-Fast Socket Binding**: `create_listen_socket` validates binding and socket creation errors immediately with detailed descriptive messages.

---

## Linux `io_uring` Tuning

Aegon exposes direct knobs for kernel I/O submission ring tuning:

```cpp
// Configure ring capacity (default: 4096 SQEs)
server.ring_entries(8192);

// Enable SQPOLL: Kernel polling thread for zero syscall overhead
// Parameters: enable, idle_ms (kernel thread sleep threshold), cpu (pinned core, -1 = any)
server.enable_sqpoll(true, 2000, 2);
```

### What is SQPOLL?
With `IORING_SETUP_SQPOLL`, the Linux kernel spawns a dedicated kernel thread that continually polls the submission queue. The application produces I/O requests to ring buffers in user space without executing `io_uring_enter()` syscalls, enabling true zero-syscall network processing.

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
