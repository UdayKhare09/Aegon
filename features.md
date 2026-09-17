# Aegon Framework — Feature & Developer API Reference

A comprehensive, production-grade guide to every developer-facing feature, class, method, builder, and utility function provided by the **Aegon** C++26 high-performance asynchronous web framework and compile-time ORM.

---

## Table of Contents

1. [Core Architecture & Asynchronous I/O (`aegon::core`)](#1-core-architecture--asynchronous-io-aegoncore)
   - [Task Coroutines (`Task<T>`)](#task-coroutines-taskt)
   - [io_uring Engine (`IoUringConfig`, `IoUring`)](#io_uring-engine-iouringconfig-iouring)
   - [Multishot Accept Stream (`MultishotAcceptStream`)](#multishot-accept-stream-multishotacceptstream)
   - [Zero-Copy Buffer Pool (`BufferPool`)](#zero-copy-buffer-pool-bufferpool)
   - [Per-Core Event Loop (`EventLoop`)](#per-core-event-loop-eventloop)
2. [HTTP Engine & Web Server (`aegon::http`)](#2-http-engine--web-server-aegonhttp)
   - [Server (`Server`)](#server-server)
   - [Router & Route Matching (`Router`, `RouteGroup`)](#router--route-matching-router-routegroup)
   - [SIMD URL Matching (`SimdRouter`)](#simd-url-matching-simdrouter)
   - [HTTP Context (`Context`)](#http-context-context)
   - [HTTP Request (`Request`)](#http-request-request)
   - [HTTP Response (`Response`)](#http-response-response)
   - [Header Map (`HeaderMap`)](#header-map-headermap)
   - [TLS Configuration (`TlsContext`)](#tls-configuration-tlscontext)
3. [High-Performance Data Types (`aegon::data::types` & `aegon::data::uuid`)](#3-high-performance-data-types-aegondatatypes--aegondatauuid)
   - [UUID (`UUID`, `UUIDGenerator`)](#uuid-uuid-uuidgenerator)
   - [Date and Time (`DateTime`, `Date`, `Time`)](#date-and-time-datetime-date-time)
   - [Arbitrary-Precision Decimal (`Decimal<Precision, Scale>`, `Decimal128`)](#arbitrary-precision-decimal-decimalprecision-scale-decimal128)
   - [Fast Zero-Allocation JSON (`Json`)](#fast-zero-allocation-json-json)
   - [Network Addresses (`IpAddress`, `MacAddress`)](#network-addresses-ipaddress-macaddress)
   - [Binary & Cryptographic Primitives (`Blob`, `Hash256`)](#binary--cryptographic-primitives-blob-hash256)
   - [Data Validation Engine (`Validator`, `ValidationRules`)](#data-validation-engine-validator-validationrules)
4. [Compile-Time SQL ORM (`aegon::data::orm::sql`)](#4-compile-time-sql-orm-aegondataormsql)
   - [Schema Definition (`TableDef<T>`, `ColumnDef`)](#schema-definition-tabledeft-columndef)
   - [Relation Mappings (`HasOne`, `HasMany`, `BelongsTo`, `ManyToMany`)](#relation-mappings-hasone-hasmany-belongsto-manytomany)
   - [Fluent Query Builders (`SelectBuilder`, `InsertBuilder`, `UpdateBuilder`, `DeleteBuilder`)](#fluent-query-builders-selectbuilder-insertbuilder-updatebuilder-deletebuilder)
   - [Query Expressions (`col(...)`)](#query-expressions-col)
   - [Database Client & Accessor (`SqlDatabaseClient`, `SqlAccessor`)](#database-client--accessor-sqldatabaseclient-sqlaccessor)
   - [Transactions (`Transaction`)](#transactions-transaction)
   - [Per-Core Connection Pool (`PerCoreConnectionPool`)](#per-core-connection-pool-percoreconnectionpool)
   - [Schema Generation (`SchemaGenerator`)](#schema-generation-schemagenerator)
   - [Pagination Results (`Page<T>`)](#pagination-results-paget)
5. [Native Asynchronous Redis Client (`aegon::data::redis`)](#5-native-asynchronous-redis-client-aegondataredis)
   - [Deployment Topologies (Standalone, Sentinel, Cluster)](#deployment-topologies-standalone-sentinel-cluster)
   - [RESP2 & RESP3 Parser/Serializer (`Resp3Parser`, `Resp3Serializer`)](#resp2--resp3-parserserializer-resp3parser-resp3serializer)
   - [Connection Pool & RAII Guard (`RedisConnectionPool`, `Guard`)](#connection-pool--raii-guard-redisconnectionpool-guard)
   - [CRC16 & Redis Cluster Router (`Crc16`, `RedisClusterRouter`)](#crc16--redis-cluster-router-crc16-redisclusterrouter)
   - [Developer Redis API (`RedisClient`)](#developer-redis-api-redisclient)
   - [HTTP Context Accessor (`ctx.redis`)](#http-context-accessor-ctxredis)
6. [Smart Distributed Cache & Declarative ORM Caching (`aegon::data::cache` & `aegon::data::orm::sql`)](#6-smart-distributed-cache--declarative-orm-caching-aegondatacache--aegondataormsql)
   - [Pluggable Cache Backend (`CacheBackend`, `RedisCacheBackend`)](#pluggable-cache-backend-cachebackend-rediscachebackend)
   - [Declarative `TableDef` Cache Configuration](#declarative-tabledef-cache-configuration)
   - [Invalidation Strategies (`StrictEpoch`, `Partitioned`, `TtlOnly`)](#invalidation-strategies-strictepoch-partitioned-ttlonly)
   - [Normalized Two-Phase Query Pointer Caching (`.cached()`)](#normalized-two-phase-query-pointer-caching-cached)
   - [Automated Mutation Invalidation (`insert`, `update_entity`, `delete_by_id`)](#automated-mutation-invalidation-insert-update_entity-delete_by_id)

---

## 1. Core Architecture & Asynchronous I/O (`aegon::core`)

Header files: `<aegon/core/Task.hpp>`, `<aegon/core/IoUring.hpp>`, `<aegon/core/BufferPool.hpp>`, `<aegon/core/EventLoop.hpp>`

### Task Coroutines (`Task<T>`)

Aegon's native C++26 lazy coroutine task type for zero-overhead asynchronous control flow.

#### Developer Functions & Operators

| Method / Operator | Signature | Description |
|---|---|---|
| `operator co_await()` | `auto operator co_await() noexcept` | Suspends caller and executes the task on the current event loop. |
| `is_ready()` | `bool is_ready() const noexcept` | Checks if the coroutine task has completed execution. |
| `resume()` | `void resume()` | Explicitly resumes the coroutine handle. |
| `result()` | `T& result()` / `T&& result()` | Retrieves the returned value (or rethrows any unhandled exception). |

```cpp
aegon::core::Task<int> compute_async() {
    co_return 42;
}

aegon::core::Task<void> handle_async() {
    int val = co_await compute_async();
}
```

---

### io_uring Engine (`IoUringConfig`, `IoUring`)

Hardware-interfaced Linux `io_uring` wrapper supporting kernel bypass, fixed buffer pools, SQPOLL, and multishot operations.

#### `IoUringConfig` Configuration Fields

| Field | Type | Default | Description |
|---|---|---|---|
| `entries` | `uint32_t` | `4096` | Submission queue and completion queue ring capacity. |
| `flags` | `uint32_t` | `0` | Kernel ring setup flags (e.g. `IORING_SETUP_COOP_TASKRUN`). |
| `enable_sqpoll` | `bool` | `false` | Enables kernel thread submission queue polling (`IORING_SETUP_SQPOLL`). |
| `sq_thread_idle_ms`| `uint32_t` | `2000` | Milliseconds the kernel SQPOLL thread sleeps when idle. |
| `sq_thread_cpu` | `uint32_t` | `0` | CPU core pin target for the SQPOLL kernel thread (`IORING_SETUP_SQ_AFF`). |

#### `IoUring` Developer Methods

| Method | Signature | Description |
|---|---|---|
| `accept()` | `Task<int> accept(int listen_fd, sockaddr* addr = nullptr, socklen_t* addrlen = nullptr)` | Asynchronously accepts a new TCP connection socket. |
| `accept_multishot()` | `MultishotAcceptStream accept_multishot(int listen_fd)` | Uses `IORING_ACCEPT_MULTISHOT` to automatically accept incoming connections without re-submitting SQEs. |
| `recv_multishot()` | `Task<RecvResult> recv_multishot(int fd, uint16_t bgid)` | Receives data using provided buffer rings (`IOSQE_BUFFER_SELECT`). |
| `send()` | `Task<int> send(int fd, const void* buf, size_t len, int flags = 0)` | Asynchronously transmits a buffer over a socket. |
| `send_zc()` | `Task<int> send_zc(int fd, const void* buf, size_t len, int flags = 0)` | Kernel zero-copy send (`IORING_OP_SEND_ZC`), bypassing page copying. |
| `send_zc_fixed()` | `Task<int> send_zc_fixed(int fd, const void* buf, size_t len, uint16_t buf_index, int flags = 0)` | Kernel zero-copy send using pre-registered fixed buffer addresses. |
| `splice()` | `Task<int> splice(int fd_in, int64_t off_in, int fd_out, int64_t off_out, size_t nbytes, unsigned int flags)` | Direct zero-copy kernel pipe/file-to-socket transfer (`IORING_OP_SPLICE`). |
| `close()` | `Task<int> close(int fd)` | Asynchronously closes an open file/socket descriptor. |
| `cancel()` | `Task<int> cancel(uint64_t user_data)` | Cancels an in-flight io_uring operation by its correlation ID. |
| `recvmsg()` | `Task<int> recvmsg(int fd, msghdr* msg, unsigned int flags = 0)` | Asynchronous scatter-gather datagram read (UDP / HTTP/3). |
| `timeout()` | `Task<int> timeout(__kernel_timespec* ts)` | Asynchronously suspends the coroutine for a kernel-timed interval. |
| `process_completions()` | `int process_completions()` | Dispatches all ready CQEs to their awaiting coroutine tasks. |
| `submit_and_wait()` | `int submit_and_wait(int wait_nr = 0)` | Flushes SQEs to the kernel and optionally waits for completions. |
| `is_sqpoll_enabled()` | `bool is_sqpoll_enabled() const noexcept` | Returns whether SQPOLL mode is active. |
| `raw_ring()` | `io_uring* raw_ring() noexcept` | Accesses the underlying liburing `struct io_uring` pointer. |

---

### Multishot Accept Stream (`MultishotAcceptStream`)

Streaming iterator returned by `IoUring::accept_multishot()`.

| Method | Signature | Description |
|---|---|---|
| `next()` | `Task<int> next()` | Awaits the next incoming client connection descriptor. |
| `cancel()` | `Task<void> cancel()` | Cancels the multishot accept operation in the kernel. |
| `is_armed()` | `bool is_armed() const noexcept` | Returns true if the kernel multishot accept SQE is active. |
| `listen_fd()` | `int listen_fd() const noexcept` | Returns the listening server socket descriptor. |

---

### Zero-Copy Buffer Pool (`BufferPool`)

High-performance memory slab management utilizing kernel-managed buffer rings (`IORING_REGISTER_PBUF_RING`) and pre-registered fixed buffers (`IORING_REGISTER_BUFFERS`).

| Method | Signature | Description |
|---|---|---|
| `get_buffer()` | `std::span<char> get_buffer(uint16_t bid)` | Resolves a kernel-provided buffer ID into an active memory span. |
| `return_buffer()` | `void return_buffer(uint16_t bid)` | Returns a used buffer back to the kernel `io_uring_buf_ring`. |
| `bgid()` | `uint16_t bgid() const noexcept` | Returns the buffer group ID recognized by the kernel. |
| `buffer_size()` | `size_t buffer_size() const noexcept` | Returns individual buffer chunk size (e.g. 4096 bytes). |
| `entries()` | `size_t entries() const noexcept` | Returns total number of buffers registered in the pool. |
| `is_registered()` | `bool is_registered() const noexcept` | Returns true if pre-registered with `IORING_REGISTER_BUFFERS`. |
| `registered_buf_index()`| `uint16_t registered_buf_index(const void* ptr) const` | Finds the registered fixed buffer index for a memory address. |
| `raw_memory()` | `void* raw_memory() noexcept` | Accesses the contiguous underlying virtual memory pool. |

---

### Per-Core Event Loop (`EventLoop`)

Thread-pinned execution context managing an `IoUring` instance, buffer pools, and coroutine task dispatching.

| Method | Signature | Description |
|---|---|---|
| `run()` | `void run()` | Starts the polling and execution loop until `stop()` is invoked. |
| `stop()` | `void stop()` | Signals the event loop to terminate gracefully. |
| `pin_to_core()` | `void pin_to_core(int core_id)` | Pins the calling OS thread to a specific CPU core (`pthread_setaffinity_np`). |
| `spawn()` | `void spawn(Task<void> task)` | Spawns a detached root coroutine task on the event loop. |
| `ring()` | `IoUring& ring() noexcept` | Accesses the loop's io_uring instance. |
| `buffer_pool()` | `BufferPool& buffer_pool() noexcept` | Accesses the thread's zero-copy buffer pool. |
| `is_running()` | `bool is_running() const noexcept` | Checks if the event loop is actively executing. |

---

## 2. HTTP Engine & Web Server (`aegon::http`)

Header files: `<aegon/http/Server.hpp>`, `<aegon/http/Router.hpp>`, `<aegon/http/SimdRouter.hpp>`, `<aegon/http/Context.hpp>`, `<aegon/http/Request.hpp>`, `<aegon/http/Response.hpp>`, `<aegon/http/HeaderMap.hpp>`, `<aegon/http/Tls.hpp>`

### Server (`Server`)

Core asynchronous multithreaded web server engine supporting HTTP/1.1, HTTP/2, HTTP/3, TLS, and SQPOLL.

| Method | Signature | Description |
|---|---|---|
| `set_router()` | `Server& set_router(Router router)` | Sets the root routing table for the server. |
| `router()` | `Router& router() noexcept` | Accesses the server's internal router for inline route declarations. |
| `listen()` | `Server& listen(std::string host, uint16_t port)` | Sets the bind address and TCP listening port. |
| `enable_tls()` | `Server& enable_tls(std::string cert_path, std::string key_path)` | Configures TLS encryption with certificate and private key paths. |
| `enable_http3()` | `Server& enable_http3(bool enable = true)` | Enables QUIC and HTTP/3 UDP listeners on the server port. |
| `enable_sqpoll()` | `Server& enable_sqpoll(bool enable = true)` | Enables kernel-thread SQPOLL on all worker io_uring instances. |
| `ring_entries()` | `Server& ring_entries(uint32_t entries)` | Configures io_uring queue depth (default 4096). |
| `set_state()` | `template<typename T> Server& set_state(std::shared_ptr<T> state)` | Registers shared application state accessible via `Context::state<T>()`. |
| `sql_client()` | `Server& sql_client(std::shared_ptr<SqlDatabaseClient> client)` | Attaches a global SQL ORM client instance to the server. |
| `db.sql` | `SqlAccessor db.sql` | Server-level fluent SQL ORM accessor for background queries. |
| `run()` | `void run()` | Runs the server synchronously on the main thread. |
| `run(threads)` | `void run(size_t threads)` | Spawns an `SO_REUSEPORT` thread-per-core event loop cluster. |
| `stop()` | `void stop()` | Gracefully stops the server and closes all listening sockets. |
| `port()` | `uint16_t port() const noexcept` | Returns configured listening port. |
| `host()` | `std::string_view host() const noexcept` | Returns configured listening host. |
| `is_tls_enabled()` | `bool is_tls_enabled() const noexcept` | Checks if TLS is enabled. |
| `is_http3_enabled()`| `bool is_http3_enabled() const noexcept` | Checks if HTTP/3 is enabled. |
| `is_sqpoll_enabled()`| `bool is_sqpoll_enabled() const noexcept` | Checks if SQPOLL is enabled. |

```cpp
#include <aegon/http/Server.hpp>

int main() {
    aegon::http::Server app;
    app.listen("0.0.0.0", 8080)
       .enable_sqpoll(true);

    app.router().get("/ping", [](aegon::http::Context& ctx) -> aegon::core::Task<void> {
        ctx.res().text("pong");
        co_return;
    });

    app.run(std::thread::hardware_concurrency());
}
```

---

### Router & Route Matching (`Router`, `RouteGroup`)

Prefix Trie and SIMD-accelerated router with exact match maps and parameter extraction (`:id`, `*wildcard`).

#### `Router` Methods

| Method | Signature | Description |
|---|---|---|
| `get()` | `Router& get(std::string path, Handler handler)` | Registers a `GET` handler. |
| `post()` | `Router& post(std::string path, Handler handler)` | Registers a `POST` handler. |
| `put()` | `Router& put(std::string path, Handler handler)` | Registers a `PUT` handler. |
| `delete_()` | `Router& delete_(std::string path, Handler handler)` | Registers a `DELETE` handler. |
| `patch()` | `Router& patch(std::string path, Handler handler)` | Registers a `PATCH` handler. |
| `head()` | `Router& head(std::string path, Handler handler)` | Registers a `HEAD` handler. |
| `options()` | `Router& options(std::string path, Handler handler)` | Registers an `OPTIONS` handler. |
| `all()` | `Router& all(std::string path, Handler handler)` | Matches any HTTP method on the specified route. |
| `route()` | `Router& route(Method method, std::string path, Handler handler)` | Registers a custom method handler. |
| `use()` | `Router& use(Middleware middleware)` | Registers global middleware executed prior to routes. |
| `group()` | `RouteGroup group(std::string prefix)` | Creates a prefixed route sub-tree. |
| `match()` | `MatchResult match(Method method, std::string_view path)` | Matches a request method and path, extracting URL parameters. |

#### `RouteGroup` Methods

| Method | Signature | Description |
|---|---|---|
| `get()`, `post()`, `put()`, `delete_()`, `patch()`, `head()`, `options()`, `all()`, `route()` | *(Same parameters as Router)* | Registers handlers prefixed by group's path. |
| `use()` | `RouteGroup& use(Middleware middleware)` | Attaches middleware scoped exclusively to this route group. |
| `group()` | `RouteGroup group(std::string sub_prefix)` | Creates nested sub-groups (e.g. `/api/v1/users`). |
| `prefix()` | `std::string_view prefix() const noexcept` | Returns current combined prefix string. |

---

### SIMD URL Matching (`SimdRouter`)

Vectorized string matching utilities using AVX2 or ARM NEON instructions.

| Method | Signature | Description |
|---|---|---|
| `simd_common_prefix()` | `static size_t simd_common_prefix(const char* a, const char* b, size_t max_len)` | Computes longest common prefix using 128/256-bit SIMD vector equality. |
| `simd_find_char()` | `static const char* simd_find_char(const char* s, size_t len, char target)` | Scans a buffer for a character (e.g. `/`, `?`, `:`) with SIMD registers. |
| `simd_starts_with()` | `static bool simd_starts_with(std::string_view str, std::string_view prefix)` | Vectorized prefix check with unaligned SIMD loads. |

---

### HTTP Context (`Context`)

Per-request transaction context passed to every route handler and middleware.

| Method | Signature | Description |
|---|---|---|
| `req()` | `Request& req() noexcept` | Returns mutable reference to the incoming HTTP request. |
| `res()` | `Response& res() noexcept` | Returns mutable reference to the outgoing HTTP response. |
| `bind_json<T>()` | `template<typename T> std::optional<T> bind_json()` | Deserializes the request body directly into struct `T` using Glaze. |
| `bind_query<T>()` | `template<typename T> std::optional<T> bind_query()` | Deserializes query parameters into struct `T`. |
| `bind_path<T>()` | `template<typename T> std::optional<T> bind_path()` | Deserializes route path parameters (e.g. `:id`) into struct `T`. |
| `send_file()` | `void send_file(std::string path)` | Directs the engine to serve a file via zero-copy `sendfile`/`splice`. |
| `state<T>()` | `template<typename T> std::shared_ptr<T> state()` | Retrieves typed dependency-injected application state. |
| `db.sql` | `SqlAccessor db.sql` | Context-scoped SQL accessor with connection reuse. |

---

### HTTP Request (`Request`)

Developer-facing HTTP request inspection and data extraction.

| Method | Signature | Description |
|---|---|---|
| `method()` | `Method method() const noexcept` | HTTP verb enum (`GET`, `POST`, `PUT`, `DELETE`, etc.). |
| `method_string()` | `std::string_view method_string() const noexcept` | Method string representation (e.g. `"GET"`). |
| `path()` | `std::string_view path() const noexcept` | Decoded request URI path (e.g. `"/users/42"`). |
| `query()` | `std::string_view query() const noexcept` | Raw URL query string without `?` (e.g. `"page=1&limit=20"`). |
| `headers()` | `const HeaderMap& headers() const noexcept` | Read-only access to request headers. |
| `body()` | `std::string_view body() const noexcept` | Raw payload payload string view. |
| `param()` | `std::string_view param(std::string_view key) const` | Looks up captured path parameter by token name (e.g. `"id"`). |
| `params()` | `const std::vector<std::pair<std::string, std::string>>& params() const` | Returns all path parameter key-value pairs. |
| `version()` | `Version version() const noexcept` | Protocol version enum (`HTTP_1_0`, `HTTP_1_1`, `HTTP_2_0`, `HTTP_3_0`). |
| `is_upgrade_h2c()` | `bool is_upgrade_h2c() const noexcept` | Returns true if the client requested an HTTP/2 cleartext upgrade. |
| `expect_continue()`| `bool expect_continue() const noexcept` | Returns true if client sent `Expect: 100-continue`. |
| `bind_json<T>()` | `template<typename T> std::optional<T> bind_json() const` | Deserializes JSON body into `T`. |
| `bind_query<T>()` | `template<typename T> std::optional<T> bind_query() const` | Deserializes query parameters into `T`. |
| `bind_path<T>()` | `template<typename T> std::optional<T> bind_path() const` | Deserializes URL path parameters into `T`. |

---

### HTTP Response (`Response`)

Fluent response builder for headers, payloads, JSON, files, and chunked streaming.

| Method | Signature | Description |
|---|---|---|
| `status()` | `Response& status(StatusCode code)` | Sets numeric HTTP status (e.g. `StatusCode::OK`, `StatusCode::NotFound`). |
| `header()` | `Response& header(std::string_view key, std::string_view value)` | Appends a header using zero-copy string views. |
| `set_header_owned()` | `Response& set_header_owned(std::string key, std::string value)` | Appends a header taking ownership of dynamic strings. |
| `headers()` | `const HeaderMap& headers() const noexcept` | Accesses all configured response headers. |
| `body()` | `Response& body(std::string content)` | Sets raw payload body bytes. |
| `text()` | `Response& text(std::string content)` | Sets payload and sets `Content-Type: text/plain; charset=utf-8`. |
| `json()` | `Response& json(std::string json_str)` | Sets payload and sets `Content-Type: application/json`. |
| `json<T>()` | `template<typename T> Response& json(const T& data)` | Serializes `data` to JSON via Glaze and sets `Content-Type: application/json`. |
| `html()` | `Response& html(std::string content)` | Sets payload and sets `Content-Type: text/html; charset=utf-8`. |
| `file()` | `Response& file(std::string file_path)` | Configures zero-copy static file streaming. |
| `has_file()` | `bool has_file() const noexcept` | Returns true if a static file was scheduled for streaming. |
| `file_path()` | `std::string_view file_path() const noexcept` | Returns configured static file path. |
| `file_size()` | `size_t file_size() const noexcept` | Returns size of scheduled static file in bytes. |
| `uuid()` | `Response& uuid(const aegon::data::uuid::UUID& id)` | Sets UUID string payload and `Content-Type: text/plain`. |
| `chunked()` | `Response& chunked(bool enable = true)` | Enables HTTP/1.1 chunked transfer encoding (`Transfer-Encoding: chunked`). |
| `is_chunked()` | `bool is_chunked() const noexcept` | Returns true if chunked encoding is active. |
| `serialize_http1()`| `std::string serialize_http1() const` | Serializes complete HTTP/1.1 status line, headers, and body. |
| `serialize_http1_headers()`| `std::string serialize_http1_headers() const` | Serializes only status line and headers (for zero-copy body dispatch). |
| `serialize_chunk()`| `static std::string serialize_chunk(std::string_view data)` | Formats a chunk frame (`<hex_size>\r\n<data>\r\n`). |
| `serialize_chunk_end()`| `static std::string_view serialize_chunk_end()` | Returns terminating chunk frame (`"0\r\n\r\n"`). |
| `infer_mime_type()`| `static std::string_view infer_mime_type(std::string_view path)` | Determines MIME type from file extension. |

---

### Header Map (`HeaderMap`)

Vector-backed, cache-friendly HTTP header map with case-insensitive ASCII comparison.

| Method | Signature | Description |
|---|---|---|
| `set()` | `void set(std::string_view key, std::string_view value)` | Sets or updates a header value. |
| `get()` | `std::optional<std::string_view> get(std::string_view key) const` | Looks up header value case-insensitively. |
| `contains()` | `bool contains(std::string_view key) const` | Checks if a header is present. |
| `remove()` | `void remove(std::string_view key)` | Deletes a header entry. |
| `clear()` | `void clear() noexcept` | Removes all headers. |
| `size()` | `size_t size() const noexcept` | Returns number of headers. |
| `empty()` | `bool empty() const noexcept` | Returns true if empty. |
| `begin()`, `end()` | `iterator begin()`, `iterator end()` | Iterates over `HeaderEntry` pairs (`key`, `value`). |

---

### TLS Configuration (`TlsContext`)

OpenSSL / BoringSSL wrapper for TLS 1.3 encryption and ALPN protocol negotiation (`h2`, `http/1.1`).

| Method | Signature | Description |
|---|---|---|
| `TlsContext()` | `TlsContext(std::string cert_path, std::string key_path)` | Loads X.509 certificate chain and private key. |
| `is_valid()` | `bool is_valid() const noexcept` | Returns true if SSL context initialized and keys verified. |
| `raw_ctx()` | `SSL_CTX* raw_ctx() noexcept` | Accesses the native OpenSSL `SSL_CTX*` pointer. |

---

## 3. High-Performance Data Types (`aegon::data::types` & `aegon::data::uuid`)

Header files: `<aegon/data/uuid/UUID.hpp>`, `<aegon/data/uuid/UUIDGenerator.hpp>`, `<aegon/data/types/DateTime.hpp>`, `<aegon/data/types/Date.hpp>`, `<aegon/data/types/Time.hpp>`, `<aegon/data/types/Decimal.hpp>`, `<aegon/data/types/Json.hpp>`, `<aegon/data/types/IpAddress.hpp>`, `<aegon/data/types/MacAddress.hpp>`, `<aegon/data/types/Blob.hpp>`, `<aegon/data/types/Hash256.hpp>`, `<aegon/data/validation/Validator.hpp>`

### UUID (`UUID`, `UUIDGenerator`)

128-bit RFC 9562 Universally Unique Identifiers with SIMD parsing and hardware entropy generation.

#### `UUID` Methods

| Method | Signature | Description |
|---|---|---|
| `UUID::nil()` | `static constexpr UUID nil() noexcept` | Returns nil UUID (`00000000-0000-0000-0000-000000000000`). |
| `is_nil()` | `constexpr bool is_nil() const noexcept` | Checks if UUID is all zeroes. |
| `version()` | `constexpr int version() const noexcept` | Returns UUID version (e.g. `4` or `7`). |
| `variant()` | `constexpr int variant() const noexcept` | Returns RFC variant number. |
| `timestamp_ms()`| `constexpr uint64_t timestamp_ms() const noexcept` | Extracts 48-bit UNIX millisecond timestamp from UUIDv7. |
| `as_bytes()` | `constexpr std::span<const uint8_t, 16> as_bytes() const noexcept` | Accesses the raw 16-byte array. |
| `bytes()` | `constexpr const std::array<uint8_t, 16>& bytes() const noexcept` | Returns underlying `std::array<uint8_t, 16>`. |
| `to_chars()` | `char* to_chars(char* out) const noexcept` | Zero-allocation SIMD format into 36-byte output buffer. |
| `to_string()` / `str()` | `std::string to_string() const` / `std::string str() const` | Returns formatted canonical 36-char string. |
| `from_string()` | `static std::optional<UUID> from_string(std::string_view str) noexcept` | Parses canonical UUID string with SIMD validation. |
| `from_chars()` | `static std::optional<UUID> from_chars(const char* str) noexcept` | Parses UUID from 36-char raw pointer buffer. |
| `fromStrFactory()`| `static UUID fromStrFactory(std::string_view str)` | Parses UUID, throwing `std::invalid_argument` on failure. |
| Comparison Ops | `==`, `!=`, `<`, `<=`, `>`, `<=>` | Three-way lexicographical comparison operators. |
| `std::hash<UUID>`| Specialization | Compatible with `std::unordered_map` and `std::unordered_set`. |

#### `UUIDGenerator` Methods

| Method | Signature | Description |
|---|---|---|
| `v4()` | `static UUID v4() noexcept` | Generates cryptographically secure random UUIDv4. |
| `getUUID()` | `static UUID getUUID() noexcept` | Alias for `v4()`. |
| `v7()` | `static UUID v7() noexcept` | Generates time-ordered sortable UUIDv7 with sub-millisecond counter. |
| `v4_batch()` | `static void v4_batch(std::span<UUID> out) noexcept` | Vectorized batch generation of UUIDv4s. |
| `v7_batch()` | `static void v7_batch(std::span<UUID> out) noexcept` | Vectorized batch generation of monotonically increasing UUIDv7s. |
| `hardware_random64()`| `static uint64_t hardware_random64() noexcept` | Hardware random number (`RDRAND` / kernel `/dev/urandom`). |
| `hardware_seed64()`| `static uint64_t hardware_seed64() noexcept` | Entropy seed from hardware clock cycles and thread IDs. |

---

### Date and Time (`DateTime`, `Date`, `Time`)

Microsecond-precision timestamp and calendar types with ISO 8601 parsing.

#### `DateTime` Methods

| Method | Signature | Description |
|---|---|---|
| `now()` / `utc_now()` | `static DateTime now() noexcept` | Current system UTC time. |
| `from_epoch_micros()` | `static constexpr DateTime from_epoch_micros(int64_t micros) noexcept` | Constructs from microsecond timestamp. |
| `from_epoch_millis()` | `static constexpr DateTime from_epoch_millis(int64_t millis) noexcept` | Constructs from millisecond timestamp. |
| `from_epoch_seconds()`| `static constexpr DateTime from_epoch_seconds(int64_t seconds) noexcept` | Constructs from second timestamp. |
| `epoch_micros()` | `constexpr int64_t epoch_micros() const noexcept` | Returns integer microseconds since UNIX epoch. |
| `epoch_millis()` | `constexpr int64_t epoch_millis() const noexcept` | Returns integer milliseconds since UNIX epoch. |
| `epoch_seconds()` | `constexpr int64_t epoch_seconds() const noexcept` | Returns integer seconds since UNIX epoch. |
| `is_epoch()` | `constexpr bool is_epoch() const noexcept` | Returns true if epoch value is 0. |
| `to_iso8601()` | `std::string to_iso8601() const` | Formats as ISO 8601 string (e.g. `"2026-09-17T23:50:00.123456Z"`). |
| `to_string()` | `std::string to_string() const` | Alias for `to_iso8601()`. |
| `from_iso8601()` | `static std::optional<DateTime> from_iso8601(std::string_view str) noexcept` | Parses ISO 8601 formatted date-time string. |
| `from_string()` | `static std::optional<DateTime> from_string(std::string_view str) noexcept` | Alias for `from_iso8601()`. |
| Operators | `+`, `-`, `+=`, `-=`, `<=>`, `==` | Time duration arithmetic and comparison. |

#### `Date` Methods

| Method | Signature | Description |
|---|---|---|
| `today()` | `static Date today() noexcept` | Current calendar date. |
| `year()` | `constexpr int32_t year() const noexcept` | Retrieves calendar year. |
| `month()` | `constexpr uint8_t month() const noexcept` | Retrieves month (1–12). |
| `day()` | `constexpr uint8_t day() const noexcept` | Retrieves day of month (1–31). |
| `is_leap_year()` | `constexpr bool is_leap_year() const noexcept` | Checks if current year is a leap year. |
| `to_chars()` | `char* to_chars(char* out) const noexcept` | Formats into `"YYYY-MM-DD"` without dynamic memory allocation. |
| `to_string()` | `std::string to_string() const` | Formats as `"YYYY-MM-DD"`. |
| `from_string()` | `static std::optional<Date> from_string(std::string_view str) noexcept` | Parses `"YYYY-MM-DD"` string. |

#### `Time` Methods

| Method | Signature | Description |
|---|---|---|
| `now()` | `static Time now() noexcept` | Current time of day. |
| `hour()` | `constexpr uint8_t hour() const noexcept` | Hour of day (0–23). |
| `minute()` | `constexpr uint8_t minute() const noexcept` | Minute of hour (0–59). |
| `second()` | `constexpr uint8_t second() const noexcept` | Second of minute (0–59). |
| `microsecond()` | `constexpr uint32_t microsecond() const noexcept` | Microsecond fraction (0–999,999). |
| `total_micros()` | `constexpr int64_t total_micros() const noexcept` | Total microseconds elapsed since midnight. |
| `to_chars()` | `char* to_chars(char* out) const noexcept` | Formats into `"HH:MM:SS.ffffff"` buffer. |
| `to_string()` | `std::string to_string() const` | Formats as `"HH:MM:SS.ffffff"`. |
| `from_string()` | `static std::optional<Time> from_string(std::string_view str) noexcept` | Parses `"HH:MM:SS[.ffffff]"` string. |

---

### Arbitrary-Precision Decimal (`Decimal<Precision, Scale>`, `Decimal128`)

Fixed-point arithmetic without floating-point IEEE 754 precision loss.

| Method | Signature | Description |
|---|---|---|
| `raw_value()` | `constexpr __int128 raw_value() const noexcept` | Retrieves underlying scaled integer representation. |
| `is_zero()` | `constexpr bool is_zero() const noexcept` | Checks if value is 0. |
| `is_negative()` | `constexpr bool is_negative() const noexcept` | Checks if negative. |
| `is_positive()` | `constexpr bool is_positive() const noexcept` | Checks if positive. |
| `to_double()` | `constexpr double to_double() const noexcept` | Converts to `double` (for display). |
| `to_int64()` | `constexpr int64_t to_int64() const noexcept` | Truncates fractional digits to 64-bit integer. |
| `to_string()` | `std::string to_string() const` | Formats to decimal string with exact scale. |
| `from_string()` | `static std::optional<Decimal> from_string(std::string_view str) noexcept` | Parses string into exact scaled decimal. |
| Arithmetic Ops | `+`, `-`, `*`, `/`, `+=`, `-=`, `*=`, `/=` | Exact arithmetic with scale preservation. |
| Comparison Ops | `==`, `!=`, `<`, `<=`, `>`, `<=>` | Exact comparison operators. |

---

### Fast Zero-Allocation JSON (`Json`)

Lightweight wrapper around raw JSON strings with compile-time Glaze serialization/deserialization.

| Method | Signature | Description |
|---|---|---|
| `str()` / `raw()` | `const std::string& str() const noexcept` | Accesses internal JSON string. |
| `view()` | `std::string_view view() const noexcept` | Returns string_view of JSON contents. |
| `c_str()` | `const char* c_str() const noexcept` | Returns null-terminated C string pointer. |
| `empty()` | `bool empty() const noexcept` | Checks if JSON string is empty. |
| `size()` | `size_t size() const noexcept` | Returns byte length of JSON string. |
| `get<T>()` | `template<typename T> std::optional<T> get() const` | Deserializes JSON directly into struct `T`. |
| `Json::from<T>()` | `template<typename T> static Json from(const T& val)` | Serializes struct `T` directly into a `Json` instance. |

---

### Network Addresses (`IpAddress`, `MacAddress`)

Binary IP (IPv4 / IPv6) and MAC address primitives with subnet routing checks.

#### `IpAddress` Methods

| Method | Signature | Description |
|---|---|---|
| `is_ipv4()` | `constexpr bool is_ipv4() const noexcept` | Returns true if address is IPv4. |
| `is_ipv6()` | `constexpr bool is_ipv6() const noexcept` | Returns true if address is IPv6. |
| `as_bytes()` | `std::span<const uint8_t> as_bytes() const noexcept` | Returns raw address bytes (4 or 16). |
| `byte_count()` | `size_t byte_count() const noexcept` | Returns 4 or 16. |
| `is_loopback()` | `bool is_loopback() const noexcept` | Checks if `127.0.0.1` or `::1`. |
| `is_private()` | `bool is_private() const noexcept` | Checks if RFC 1918 / RFC 4193 private range. |
| `in_subnet()` | `bool in_subnet(std::string_view cidr) const` | Checks if address belongs to a CIDR range (e.g. `"192.168.1.0/24"`). |
| `to_string()` | `std::string to_string() const` | Formats standard IP string. |
| `from_string()` | `static std::optional<IpAddress> from_string(std::string_view str) noexcept` | Parses IPv4 or IPv6 string. |

#### `MacAddress` Methods

| Method | Signature | Description |
|---|---|---|
| `as_bytes()` | `std::span<const uint8_t, 6> as_bytes() const noexcept` | Returns raw 6-byte hardware address. |
| `to_chars()` | `char* to_chars(char* out) const noexcept` | Formats into `"XX:XX:XX:XX:XX:XX"` buffer. |
| `to_string()` | `std::string to_string() const` | Formats canonical MAC string. |
| `from_string()` | `static std::optional<MacAddress> from_string(std::string_view str) noexcept` | Parses `"XX:XX:XX:XX:XX:XX"` or `"XX-XX-XX-XX-XX-XX"`. |

---

### Binary & Cryptographic Primitives (`Blob`, `Hash256`)

#### `Blob` Methods

| Method | Signature | Description |
|---|---|---|
| `data()` | `const uint8_t* data() const noexcept` | Returns raw byte pointer. |
| `size()` | `size_t size() const noexcept` | Returns length in bytes. |
| `empty()` | `bool empty() const noexcept` | Checks if buffer is empty. |
| `bytes()` | `const std::vector<uint8_t>& bytes() const noexcept` | Accesses vector storage. |
| `to_base64()` | `std::string to_base64() const` | Encodes binary data to Base64 string. |
| `from_base64()` | `static std::optional<Blob> from_base64(std::string_view str)` | Decodes Base64 string into `Blob`. |
| `to_hex()` | `std::string to_hex() const` | Encodes binary data to hex string. |
| `from_hex()` | `static std::optional<Blob> from_hex(std::string_view str)` | Decodes hexadecimal string into `Blob`. |

#### `Hash256` Methods

| Method | Signature | Description |
|---|---|---|
| `data()` | `const uint8_t* data() const noexcept` | Accesses 32-byte fixed buffer. |
| `size()` | `constexpr size_t size() const noexcept` | Returns 32. |
| `to_hex()` / `to_string()`| `std::string to_hex() const` | Formats as 64-character lowercase hex string. |
| `from_hex()` / `from_string()`| `static std::optional<Hash256> from_hex(std::string_view str) noexcept` | Parses 64-character hex string. |
| `operator==` | `bool operator==(const Hash256& other) const noexcept` | **Constant-time** cryptographic comparison to prevent timing attacks. |

---

### Data Validation Engine (`Validator`, `ValidationRules`)

Declarative schema and field validation engine producing structured error reports.

#### `ValidationRules` Methods

| Method | Signature | Description |
|---|---|---|
| `field()` | `FieldValidator<T>& field(std::string name, const T& value)` | Creates a validation rule set for a field. |
| `has_violations()`| `bool has_violations() const noexcept` | Returns true if any validation rule failed. |
| `violations()` | `const std::vector<Violation>& violations() const noexcept`| Returns vector of violation objects (`field`, `message`). |
| `to_json()` | `std::string to_json() const` | Serializes validation failures to standard JSON error format. |
| `nested()` | `void nested(std::string prefix, const ValidationRules& sub_rules)` | Merges child validation rules into this rule set. |
| `nested_each()` | `template<typename Iterable, typename Fn> void nested_each(...)` | Validates a collection of objects element-by-element. |

#### `FieldValidator<T>` Methods

| Method | Signature | Description |
|---|---|---|
| `required()` | `FieldValidator& required(std::string message = "...")` | Ensures string or optional field is non-empty. |
| `min_len()` | `FieldValidator& min_len(size_t len, std::string message = "...")` | Validates minimum string length. |
| `max_len()` | `FieldValidator& max_len(size_t len, std::string message = "...")` | Validates maximum string length. |
| `email()` | `FieldValidator& email(std::string message = "...")` | Validates RFC 5322 email address format. |
| `min()` | `FieldValidator& min(T val, std::string message = "...")` | Validates lower numerical bound ($x \ge \text{val}$). |
| `max()` | `FieldValidator& max(T val, std::string message = "...")` | Validates upper numerical bound ($x \le \text{val}$). |
| `positive()` | `FieldValidator& positive(std::string message = "...")` | Validates that numeric value is $> 0$. |
| `not_nil()` | `FieldValidator& not_nil(std::string message = "...")` | Validates that UUID is not `UUID::nil()`. |
| `past()` | `FieldValidator& past(std::string message = "...")` | Ensures `DateTime` is before `DateTime::now()`. |
| `future()` | `FieldValidator& future(std::string message = "...")` | Ensures `DateTime` is after `DateTime::now()`. |
| `custom()` | `FieldValidator& custom(std::function<bool(const T&)> fn, ...)`| Evaluates custom lambda predicate. |

```cpp
aegon::data::validation::ValidationRules rules;
rules.field("email", user.email).required().email();
rules.field("age", user.age).min(18).max(120);

if (rules.has_violations()) {
    ctx.res().status(aegon::http::StatusCode::UnprocessableEntity).json(rules.to_json());
}
```

---

## 4. Compile-Time SQL ORM (`aegon::data::orm::sql`)

Header files: `<aegon/data/orm/sql/TableDef.hpp>`, `<aegon/data/orm/sql/QueryBuilder.hpp>`, `<aegon/data/orm/sql/Relation.hpp>`, `<aegon/data/orm/sql/SqlDatabaseClient.hpp>`, `<aegon/data/orm/sql/Transaction.hpp>`, `<aegon/data/orm/sql/SchemaGenerator.hpp>`, `<aegon/data/orm/sql/Page.hpp>`

### Schema Definition (`TableDef<T>`, `ColumnDef`)

Compile-time table registration mapping C++ struct fields to SQL database columns.

| Method | Signature | Description |
|---|---|---|
| `TableDef::id()` | `auto id(MemberPtr ptr, std::string col_name = "id")` | Configures primary key with auto-increment or UUID generation. |
| `TableDef::column()` | `auto column(MemberPtr ptr, std::string col_name)` | Declares a column mapped to a struct member variable. |
| `unique()` | `ColumnDef& unique(bool u = true)` | Adds `UNIQUE` constraint to column. |
| `nullable()` | `ColumnDef& nullable(bool n = true)` | Marks column as `NULL` allowed (`std::optional<T>`). |
| `not_null()` | `ColumnDef& not_null()` | Marks column as `NOT NULL`. |
| `indexed()` | `ColumnDef& indexed(bool idx = true)` | Creates a database index on this column. |
| `length()` | `ColumnDef& length(size_t len)` | Sets `VARCHAR(len)` maximum character length. |
| `default_value()` | `ColumnDef& default_value(std::string def)` | Sets column `DEFAULT` literal. |
| `references()` | `ColumnDef& references(std::string target_table, std::string target_col)` | Defines SQL `FOREIGN KEY` reference. |
| `on_delete()` | `ColumnDef& on_delete(CascadeAction action)` | Configures foreign key delete behavior (`CASCADE`, `SET NULL`, `RESTRICT`). |
| `on_delete_cascade()`| `ColumnDef& on_delete_cascade()` | Shorthand for `ON DELETE CASCADE`. |
| `on_delete_set_null()`| `ColumnDef& on_delete_set_null()` | Shorthand for `ON DELETE SET NULL`. |
| `on_delete_restrict()`| `ColumnDef& on_delete_restrict()` | Shorthand for `ON DELETE RESTRICT`. |
| `created_at()` | `TableDef& created_at(MemberPtr ptr, std::string col = "created_at")` | Auto-populates creation timestamp on insert. |
| `updated_at()` | `TableDef& updated_at(MemberPtr ptr, std::string col = "updated_at")` | Auto-updates timestamp on record modification. |

---

### Relation Mappings (`HasOne`, `HasMany`, `BelongsTo`, `ManyToMany`)

Type-safe ORM associations supporting eager loading (`include()`) and relation queries (`where_has()`).

| Method | Signature | Description |
|---|---|---|
| `has_one()` | `TableDef& has_one(MemberPtr ptr, std::string foreign_key)` | 1-to-1 parent association. |
| `has_many()` | `TableDef& has_many(MemberPtr ptr, std::string foreign_key)` | 1-to-N parent association. |
| `belongs_to()` | `TableDef& belongs_to(MemberPtr ptr, std::string foreign_key)` | N-to-1 child association. |
| `many_to_many()` | `TableDef& many_to_many(MemberPtr ptr, std::string pivot_table, std::string fk1, std::string fk2)` | N-to-N association using intermediate pivot join table. |

---

### Fluent Query Builders (`SelectBuilder`, `InsertBuilder`, `UpdateBuilder`, `DeleteBuilder`)

Type-safe, dialect-aware SQL query construction (`PostgreSQL`, `MySQL`, `SQLite`).

#### `SelectBuilder<Entity>` Methods

| Method | Signature | Description |
|---|---|---|
| `select()` | `SelectBuilder& select(std::vector<std::string> cols)` | Overrides selected columns (defaults to all table columns). |
| `where()` | `SelectBuilder& where(Expression expr)` | Appends a filter expression (`WHERE expr`). |
| `and_where()` | `SelectBuilder& and_where(Expression expr)` | Appends `AND expr` clause. |
| `or_where()` | `SelectBuilder& or_where(Expression expr)` | Appends `OR expr` clause. |
| `where_has()` | `SelectBuilder& where_has(std::string relation, Expression expr)` | Sub-query filter asserting associated relation matches criteria. |
| `where_doesnt_have()`| `SelectBuilder& where_doesnt_have(std::string relation)` | Asserts that no associated records exist. |
| `order_by()` | `SelectBuilder& order_by(std::string col)` | Appends `ORDER BY col ASC`. |
| `order_by_desc()` | `SelectBuilder& order_by_desc(std::string col)` | Appends `ORDER BY col DESC`. |
| `limit()` | `SelectBuilder& limit(size_t count)` | Adds `LIMIT count` clause. |
| `offset()` | `SelectBuilder& offset(size_t skip)` | Adds `OFFSET skip` clause. |
| `group_by()` | `SelectBuilder& group_by(std::string col)` | Adds `GROUP BY col` clause. |
| `having()` | `SelectBuilder& having(Expression expr)` | Adds `HAVING expr` clause. |
| `include()` | `SelectBuilder& include(std::string relation)` | Eager loads related child records in a single query. |
| `count()` | `Task<int64_t> count()` | Executes `SELECT COUNT(*)` asynchronously. |
| `sum()` | `Task<double> sum(std::string col)` | Executes `SELECT SUM(col)` asynchronously. |
| `avg()` | `Task<double> avg(std::string col)` | Executes `SELECT AVG(col)` asynchronously. |
| `min()` | `Task<double> min(std::string col)` | Executes `SELECT MIN(col)` asynchronously. |
| `max()` | `Task<double> max(std::string col)` | Executes `SELECT MAX(col)` asynchronously. |
| `paginate()` | `Task<Page<Entity>> paginate(size_t page, size_t per_page)` | Automatically paginates rows and computes page counts. |
| `to_sql()` | `std::string to_sql() const` | Renders prepared SQL string with parameter placeholders (`$1`, `?`). |

#### `InsertBuilder<Entity>` Methods

| Method | Signature | Description |
|---|---|---|
| `insert()` | `InsertBuilder& insert(const Entity& entity)` | Binds entity fields for insertion. |
| `values()` | `InsertBuilder& values(std::vector<Entity> entities)` | Batch inserts multiple records. |
| `on_conflict_do_nothing()` | `InsertBuilder& on_conflict_do_nothing()` | Generates `ON CONFLICT DO NOTHING` (PostgreSQL/SQLite) or `INSERT IGNORE` (MySQL). |
| `on_conflict_update()` | `InsertBuilder& on_conflict_update(std::vector<std::string> conflict_cols, std::vector<std::string> update_cols)` | Generates `ON CONFLICT (...) DO UPDATE SET` upsert query. |
| `to_sql()` | `std::string to_sql() const` | Renders prepared SQL insertion string. |

#### `UpdateBuilder<Entity>` Methods

| Method | Signature | Description |
|---|---|---|
| `set()` | `UpdateBuilder& set(std::string col, Value val)` | Assigns column value. |
| `where()` | `UpdateBuilder& where(Expression expr)` | Sets target filter for record updates. |
| `to_sql()` | `std::string to_sql() const` | Renders prepared SQL update query. |

#### `DeleteBuilder<Entity>` Methods

| Method | Signature | Description |
|---|---|---|
| `where()` | `DeleteBuilder& where(Expression expr)` | Sets target filter for deletion. |
| `to_sql()` | `std::string to_sql() const` | Renders prepared SQL deletion query. |

---

### Query Expressions (`col(...)`)

Type-safe conditional query operators for building `WHERE` and `HAVING` clauses.

| Function / Method | Signature | Description |
|---|---|---|
| `col("col") == val` / `eq()` | `Expression eq(Value val)` | Equal (`=`). |
| `col("col") != val` / `neq()`| `Expression neq(Value val)` | Not equal (`!=`). |
| `col("col") > val` / `gt()` | `Expression gt(Value val)` | Greater than (`>`). |
| `col("col") >= val` / `gte()`| `Expression gte(Value val)` | Greater than or equal (`>=`). |
| `col("col") < val` / `lt()` | `Expression lt(Value val)` | Less than (`<`). |
| `col("col") <= val` / `lte()`| `Expression lte(Value val)` | Less than or equal (`<=`). |
| `col("col").like()` | `Expression like(std::string pattern)` | Pattern matching (`LIKE 'pattern'`). |
| `col("col").in()` | `Expression in(std::vector<Value> vals)` | In set (`IN ($1, $2, ...)`). |
| `col("col").not_in()` | `Expression not_in(std::vector<Value> vals)` | Not in set (`NOT IN (...)`). |
| `col("col").is_null()` | `Expression is_null()` | Null check (`IS NULL`). |
| `col("col").is_not_null()` | `Expression is_not_null()` | Not null check (`IS NOT NULL`). |
| `col("col").between()` | `Expression between(Value low, Value high)` | Range check (`BETWEEN $1 AND $2`). |

---

### Database Client & Accessor (`SqlDatabaseClient`, `SqlAccessor`)

Asynchronous database connectivity handling connection pools, transactions, and entity mapping.

| Method | Signature | Description |
|---|---|---|
| `from<T>()` | `SelectBuilder<T> from<T>()` | Spawns a type-safe `SELECT` builder for entity `T`. |
| `insert<T>()` | `Task<int> insert(const T& entity)` | Inserts entity record into its configured table. |
| `insert_get_id<T>()` | `Task<int64_t> insert_get_id(const T& entity)` | Inserts record and returns generated auto-increment ID. |
| `update<T>()` | `UpdateBuilder<T> update<T>()` | Spawns an `UPDATE` builder for entity `T`. |
| `delete_from<T>()` | `DeleteBuilder<T> delete_from<T>()` | Spawns a `DELETE` builder for entity `T`. |
| `find_by_id<T>()` | `Task<std::optional<T>> find_by_id(PrimaryKeyValue id)` | Fetches single record matching primary key. |
| `delete_by_id<T>()` | `Task<bool> delete_by_id(PrimaryKeyValue id)` | Deletes single record matching primary key. |
| `execute()` | `Task<int> execute(std::string sql, std::vector<Value> params)` | Executes raw SQL mutation (`INSERT`, `UPDATE`, `DELETE`). |
| `fetch_all<T>()` | `Task<std::vector<T>> fetch_all(std::string sql, std::vector<Value> params)` | Runs raw query and maps rows into struct `T`. |
| `fetch_one<T>()` | `Task<std::optional<T>> fetch_one(std::string sql, std::vector<Value> params)` | Runs raw query and maps first row into `T`. |
| `transaction()` | `Task<Transaction> transaction()` | Checks out a dedicated connection and opens a transaction (`BEGIN`). |
| `paginate<T>()` | `Task<Page<T>> paginate(SelectBuilder<T> builder, size_t page, size_t per_page)` | Executes paginated query. |

---

### Transactions (`Transaction`)

Scoped transaction manager ensuring ACID guarantees across multiple asynchronous operations.

| Method | Signature | Description |
|---|---|---|
| `commit()` | `Task<void> commit()` | Commits all transaction modifications to the database (`COMMIT`). |
| `rollback()` | `Task<void> rollback()` | Rolls back all operations in this transaction (`ROLLBACK`). |
| `execute()` | `Task<int> execute(std::string sql, std::vector<Value> params)` | Executes raw mutation inside the transaction scope. |
| `query<T>()` | `Task<std::vector<T>> query(std::string sql, std::vector<Value> params)` | Executes query inside the transaction scope. |
| `from<T>()` | `SelectBuilder<T> from<T>()` | Fluent query builder scoped to the transaction's connection. |

---

### Per-Core Connection Pool (`PerCoreConnectionPool`)

Lock-free per-core connection pool preventing cross-thread mutex contention.

| Method | Signature | Description |
|---|---|---|
| `acquire()` | `Task<std::shared_ptr<Connection>> acquire()` | Checks out an idle database connection for the current CPU core. |
| `release()` | `void release(std::shared_ptr<Connection> conn)` | Returns connection back to the core's local pool. |
| `active_count()` | `size_t active_count() const noexcept` | Number of currently leased connections. |
| `capacity()` | `size_t capacity() const noexcept` | Maximum pool size per core. |

---

### Schema Generation (`SchemaGenerator`)

Automated DDL generator generating table schemas directly from C++ struct definitions.

| Method | Signature | Description |
|---|---|---|
| `create_table_sql<T>()` | `static std::string create_table_sql<T>(Dialect dialect)` | Produces `CREATE TABLE IF NOT EXISTS` DDL with foreign keys, indexes, and constraints. |
| `drop_table_sql<T>()` | `static std::string drop_table_sql<T>(Dialect dialect)` | Produces `DROP TABLE IF EXISTS` DDL statement. |

---

### Pagination Results (`Page<T>`)

Standard paginated result container with metadata.

| Field / Method | Type / Signature | Description |
|---|---|---|
| `items` | `std::vector<T>` | Records fetched for the current page. |
| `total_items` | `int64_t` | Total matching records across the entire dataset. |
| `current_page` | `size_t` | 1-indexed current page number. |
| `per_page` | `size_t` | Maximum records requested per page. |
| `total_pages` | `size_t` | Calculated total number of pages ($\lceil \text{total\_items} / \text{per\_page} \rceil$). |
| `has_next()` | `bool has_next() const noexcept` | Returns true if `current_page < total_pages`. |
| `has_prev()` | `bool has_prev() const noexcept` | Returns true if `current_page > 1`. |

---

## 5. End-to-End Code Example

Putting all subsystems together in an idiomatic Aegon application:

```cpp
#include <aegon/http/Server.hpp>
#include <aegon/data/uuid/UUID.hpp>
#include <aegon/data/types/DateTime.hpp>
#include <aegon/data/types/Decimal.hpp>
#include <aegon/data/orm/sql/TableDef.hpp>
#include <aegon/data/validation/Validator.hpp>

using aegon::data::uuid::UUID;
using aegon::data::uuid::UUIDGenerator;
using aegon::data::types::DateTime;
using aegon::data::types::Decimal128;
using aegon::data::orm::sql::col;

struct Product {
    UUID id;
    std::string name;
    Decimal128 price;
    DateTime created_at;
};

// Compile-Time ORM Mapping
const auto ProductTable = aegon::data::orm::sql::TableDef<Product>("products")
    .id(&Product::id, "id")
    .column(&Product::name, "name").length(255).not_null().indexed()
    .column(&Product::price, "price").not_null()
    .created_at(&Product::created_at, "created_at");

int main() {
    aegon::http::Server server;
    server.listen("0.0.0.0", 8080)
          .enable_sqpoll(true);

    auto api = server.router().group("/api/v1");

    // Paginated product query with zero-copy JSON response
    api.get("/products", [](aegon::http::Context& ctx) -> aegon::core::Task<void> {
        auto page = co_await ctx.db.sql.from<Product>()
            .where(col("price") > 10.0)
            .order_by_desc("created_at")
            .paginate(1, 20);

        ctx.res().json(page);
    });

    // Validated create route with UUIDv7 generation
    api.post("/products", [](aegon::http::Context& ctx) -> aegon::core::Task<void> {
        auto product = ctx.bind_json<Product>();
        if (!product) {
            ctx.res().status(aegon::http::StatusCode::BadRequest).text("Invalid JSON payload");
            co_return;
        }

        aegon::data::validation::ValidationRules rules;
        rules.field("name", product->name).required().min_len(3).max_len(255);
        rules.field("price", product->price.to_double()).positive();

        if (rules.has_violations()) {
            ctx.res().status(aegon::http::StatusCode::UnprocessableEntity).json(rules.to_json());
            co_return;
        }

        product->id = UUIDGenerator::v7();
        co_await ctx.db.sql.insert(*product);

        ctx.res().status(aegon::http::StatusCode::Created).json(*product);
    });

    // Zero-copy static asset download
    api.get("/downloads/:filename", [](aegon::http::Context& ctx) -> aegon::core::Task<void> {
        auto filename = ctx.req().param("filename");
        ctx.send_file(std::string("/var/www/assets/") + std::string(filename));
        co_return;
    });

    server.run(std::thread::hardware_concurrency());
}
```

---

## 5. Native Asynchronous Redis Client (`aegon::data::redis`)

Header files: `<aegon/data/redis/RedisClient.hpp>`, `<aegon/data/redis/Resp3.hpp>`, `<aegon/data/redis/RedisConnectionPool.hpp>`, `<aegon/data/redis/RedisSentinel.hpp>`, `<aegon/data/redis/RedisCluster.hpp>`

Aegon incorporates a zero-dependency, ultra-high-throughput Redis client built directly on top of Linux `io_uring` and C++26 coroutines (`Task<T>`).

### Deployment Topologies (Standalone, Sentinel, Cluster)

The client natively supports three operational modes without requiring external proxy layers:

1. **Standalone Mode**: Single node connection pool.
2. **Sentinel Mode**: Automatic master discovery and failover re-resolution via `SENTINEL get-master-addr-by-name <group>`.
3. **Cluster Mode**: High-availability 16,384 hash-slot sharding with automatic CRC16 calculation, `{hash_tag}` extraction, dynamic slot cache refresh on `-MOVED`, and redirect routing on `-ASK` (with preceding `ASKING` dispatch).

```cpp
// Standalone Configuration
RedisNodeConfig config{
    .host = "127.0.0.1",
    .port = 6379,
    .password = "secret"
};
RedisClient client(ring, config, /*pool_size=*/16);

// Cluster Configuration
ClusterConfig cluster_cfg{
    .seed_nodes = {{"10.0.0.1", 7000}, {"10.0.0.2", 7000}},
    .password = "secret"
};
RedisClient cluster_client(ring, cluster_cfg);
```

### RESP2 & RESP3 Parser/Serializer (`Resp3Parser`, `Resp3Serializer`)

Streaming zero-allocation parser and serializer conforming to RESP2 and RESP3 specifications.

#### Data Model (`RespValue`)

Supports Simple Strings, Errors, Integers, Bulk Strings, Arrays, Nulls, Booleans, Doubles, and Maps.

| Method | Signature | Description |
|---|---|---|
| `is_null()` | `bool is_null() const noexcept` | True if nil representation (`$-1\r\n` or `_\r\n`). |
| `is_string()` | `bool is_string() const noexcept` | Checks for simple string or bulk string. |
| `is_integer()` | `bool is_integer() const noexcept` | Checks if integer type (`:123\r\n`). |
| `is_error()` | `bool is_error() const noexcept` | Checks if error type (`-ERR ...`). |
| `is_array()` | `bool is_array() const noexcept` | Checks if array type (`*...`). |
| `as_string()` | `std::string as_string() const` | Returns string content. |
| `as_integer()` | `int64_t as_integer() const` | Returns 64-bit integer value. |
| `as_array()` | `const std::vector<RespValue>& as_array() const` | Returns nested elements vector. |

### Connection Pool & RAII Guard (`RedisConnectionPool`, `Guard`)

Per-core connection pooling ensuring zero contention across threads. Connections are leased via move-only `Guard` objects that automatically return the socket to the pool when dropped.

### CRC16 & Redis Cluster Router (`Crc16`, `RedisClusterRouter`)

- `crc16(std::string_view buf)`: Hardware-accelerated lookup table implementation of XMODEM polynomial `0x1021`.
- `key_slot(std::string_view key)`: Computes hash slot in `[0, 16383]`. Automatically parses hash tags `{...}` (e.g. `{user:101}:orders` and `{user:101}:profile` evaluate to the exact same hash slot).

### Developer Redis API (`RedisClient`)

Direct asynchronous operations exposed for high-frequency key-value, string, list, set, sorted set, hash, and pub/sub workflows.

| Method | Signature | Description |
|---|---|---|
| `get()` | `Task<std::optional<std::string>> get(std::string_view key)` | Retrieves string value for key. |
| `set()` | `Task<bool> set(std::string_view key, std::string_view val, std::optional<chrono::seconds> ttl = nullopt)` | Stores key-value pair with optional TTL. |
| `mget()` | `Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys)` | Batch-fetches multiple keys in single roundtrip. |
| `mset()` | `Task<bool> mset(const std::vector<std::pair<std::string, std::string>>& kvs)` | Batch-sets multiple keys. |
| `del()` | `Task<bool> del(std::string_view key)` | Deletes single key. |
| `del_many()` | `Task<int64_t> del_many(const std::vector<std::string>& keys)` | Deletes list of keys, returns count deleted. |
| `incr()` | `Task<int64_t> incr(std::string_view key)` | Increments integer key by 1. |
| `decr()` | `Task<int64_t> decr(std::string_view key)` | Decrements integer key by 1. |
| `hget()` | `Task<std::optional<std::string>> hget(std::string_view key, std::string_view field)` | Retrieves field from hash. |
| `hset()` | `Task<bool> hset(std::string_view key, std::string_view field, std::string_view val)` | Sets field in hash. |
| `lpush()` | `Task<int64_t> lpush(std::string_view key, std::string_view val)` | Prepend element to list. |
| `rpop()` | `Task<std::optional<std::string>> rpop(std::string_view key)` | Pop element from tail of list. |
| `sadd()` | `Task<bool> sadd(std::string_view key, std::string_view member)` | Adds member to set. |
| `zadd()` | `Task<bool> zadd(std::string_view key, double score, std::string_view member)` | Adds element with score to sorted set. |
| `publish()` | `Task<int64_t> publish(std::string_view channel, std::string_view msg)` | Publishes message to channel. |
| `exists()` | `Task<bool> exists(std::string_view key)` | Checks if key exists. |
| `expire()` | `Task<bool> expire(std::string_view key, std::chrono::seconds seconds)` | Sets timeout on key in seconds. |
| `pexpire()` | `Task<bool> pexpire(std::string_view key, std::chrono::milliseconds ms)` | Sets timeout on key in milliseconds. |
| `ttl()` | `Task<int64_t> ttl(std::string_view key)` | Returns remaining TTL in seconds (-2 if not exists, -1 if no TTL). |
| `pttl()` | `Task<int64_t> pttl(std::string_view key)` | Returns remaining TTL in milliseconds. |
| `persist()` | `Task<bool> persist(std::string_view key)` | Removes expiration timeout from key. |
| `select_db()` | `Task<bool> select_db(int index)` | Selects Redis logical database index. |
| `pipeline()` | `RedisPipeline pipeline()` | Creates a fluent batch pipeline reducing N roundtrips into 1. |
| `multi()` | `RedisTransaction multi()` | Creates an atomic MULTI/EXEC transaction block. |
| `subscriber()` | `RedisSubscriber subscriber()` | Creates a dedicated Pub/Sub subscription reader stream. |
| `execute()` | `Task<RespValue> execute(std::vector<std::string_view> args)` | Executes arbitrary raw Redis command. |

### Pipelines, Transactions, and Subscriptions

#### Fluent Pipeline (`RedisPipeline`)
Batches arbitrary commands into a single roundtrip io_uring write and reads multiple responses sequentially:
```cpp
auto pipe = client.pipeline();
pipe.set("p1", "v1")
    .set("p2", "v2")
    .get("p1")
    .incr("counter");
std::vector<RespValue> responses = co_await pipe.execute();
```

#### Atomic Transactions (`RedisTransaction`)
Guarantees serializable isolation via Redis `MULTI` / `EXEC`:
```cpp
auto tx = client.multi();
tx.set("bank:from", "100")
  .set("bank:to", "500")
  .incr("bank:tx_count");
std::vector<RespValue> results = co_await tx.exec();
```

#### Pub/Sub Subscriber Stream (`RedisSubscriber`)
Dedicated connection mode for real-time push message streaming:
```cpp
auto sub = client.subscriber();
co_await sub.subscribe({"events:orders", "events:users"});
while (true) {
    auto msg_opt = co_await sub.next_message();
    if (!msg_opt) break;
    std::println("Channel: {} Message: {}", msg_opt->channel, msg_opt->payload);
}
```

### HTTP Context Accessor (`ctx.redis`)

Available directly on every route handler:

```cpp
app.get("/cache-stat", [](aegon::http::Context& ctx) -> aegon::core::Task<void> {
    auto visits = co_await ctx.redis.incr("analytics:page_visits");
    ctx.res().text("Total visits: " + std::to_string(visits));
});
```

---

## 6. Smart Distributed Cache & Declarative ORM Caching (`aegon::data::cache` & `aegon::data::orm::sql`)

Header files: `<aegon/data/cache/CacheBackend.hpp>`, `<aegon/data/cache/RedisCacheBackend.hpp>`, `<aegon/data/orm/sql/Table.hpp>`, `<aegon/data/orm/sql/SelectBuilder.hpp>`

Aegon features an enterprise-grade, Spring Data-inspired declarative caching engine built into the compile-time SQL ORM.

### Pluggable Cache Backend (`CacheBackend`, `RedisCacheBackend`)

Abstract distributed caching contract allowing seamless substitution between Redis, Memcached, or custom clusters.

```cpp
class CacheBackend {
public:
    virtual ~CacheBackend() = default;
    virtual Task<std::optional<std::string>> get(std::string_view key) = 0;
    virtual Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys) = 0;
    virtual Task<bool> set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> ttl = std::nullopt) = 0;
    virtual Task<bool> del(std::string_view key) = 0;
    virtual Task<int64_t> del_many(const std::vector<std::string>& keys) = 0;
    virtual Task<int64_t> incr(std::string_view key) = 0;
};
```

Configure on server startup:
```cpp
server.cache_backend(std::make_shared<RedisCacheBackend>(redis_client, "app_prod:"));
```

### Declarative `TableDef` Cache Configuration

Entity caching rules are configured directly in the entity's compile-time schema definition:

```cpp
struct User {
    int id{0};
    std::string email;
    std::string username;
    int tenant_id{0};

    static const auto& schema() {
        static const auto s = TableDef<User>("users")
            .id(&User::id, "id")
            .column(&User::email, "email").unique()
            .column(&User::username, "username")
            .column(&User::tenant_id, "tenant_id")
            .cache({
                .ttl = std::chrono::seconds(300),
                .by_id = true,
                .invalidation = InvalidationMode::Partitioned
            })
            .by_unique(&User::email)
            .partition_by(&User::tenant_id);
        return s;
    }
};
```

#### Cache Configuration Options

| Option | Method / Field | Description |
|---|---|---|
| `ttl` | `std::chrono::seconds` | Expiration time for cached entity rows and query pointers. |
| `by_id` | `bool` | Enables automatic key caching under `<table_name>:id:<primary_key>`. |
| `by_unique()` | `.by_unique(&Entity::field)` | Sets up unique-column alias pointers pointing to the primary key. |
| `partition_by()` | `.partition_by(&Entity::parent_id)` | Scopes cache epoch invalidations to parent/tenant IDs instead of global table eviction. |
| `invalidation` | `InvalidationMode` | Selected invalidation strategy for mutations. |

### Invalidation Strategies (`StrictEpoch`, `Partitioned`, `TtlOnly`)

1. **`InvalidationMode::StrictEpoch`**:
   - Table maintains an atomic version counter: `<table_name>:epoch`.
   - Any `insert`, `update`, or `delete` issues an atomic `INCR <table_name>:epoch`.
   - Query cache keys embed the epoch (`q:<table_name>:<epoch>:<query_hash>`). Invalidates all table queries in $O(1)$ without scanning keys.

2. **`InvalidationMode::Partitioned`**:
   - For multi-tenant or parent-child structures (e.g. `tenant_id`, `org_id`, `project_id`).
   - Maintains an atomic version counter per partition: `<table_name>:part:<partition_id>:epoch`.
   - Writes to tenant A increment only tenant A's epoch; query caches for tenant B remain completely untouched and valid.

3. **`InvalidationMode::TtlOnly`**:
   - Zero write penalty on high-frequency tables (e.g. telemetry, logs, audit trails).
   - Queries and records expire naturally via Redis TTL (stale-while-revalidate pattern).

### Normalized Two-Phase Query Pointer Caching (`.cached()`)

To avoid cache bloat and stale data across multiple queries returning identical rows, queries cache **lists of primary key IDs**, not duplicated entity blobs.

```cpp
auto active_users = co_await ctx.db.sql.from<User>()
    .where(&User::tenant_id, Op::Eq, 100)
    .order_by_desc(&User::id)
    .cached(std::chrono::seconds(600)) // Mark query as cacheable
    .fetch_all();
```

#### Execution Workflow:
1. Computes deterministic query fingerprint: SHA256 of `(SQL + Parameters + Table/Partition Epoch)`.
2. Checks Redis for query pointer key (`q:<table_name>:<epoch>:<fingerprint>`).
3. **Cache Hit**: Retrieves array of IDs (e.g., `[1, 5, 23]`) and dispatches a single pipelined `MGET users:id:1 users:id:5 users:id:23`.
4. **Cache Miss**: Executes SQL query in database, records IDs into query key, and populates individual entity rows in Redis using pipelined `MSET`.

### Automated Mutation Invalidation (`insert`, `update_entity`, `delete_by_id`)

All mutations executed through `SqlDatabaseClient` / `ctx.db.sql` automatically orchestrate cache synchronization:

- **`co_await client.insert(entity)`**:
  - Sets `<table_name>:id:<new_id>` in cache.
  - Increments table or partition epoch counter.
- **`co_await client.update_entity(entity)`**:
  - Updates `<table_name>:id:<pk>` directly with the new entity attributes.
  - Increments table or partition epoch counter.
- **`co_await client.delete_by_id<Entity>(pk)`**:
  - Purges `<table_name>:id:<pk>`.
  - Increments table or partition epoch counter.

