# Advanced Redis Features

Aegon provides enterprise-grade abstractions over Redis: distributed locking, zero-RTT pipelining, atomic transactions (`MULTI`/`EXEC`), persistent Pub/Sub listeners, and consumer-group Streams.

---

## Distributed Locking (`RedisLock`)

Distributed locks protect critical shared resources across multiple microservice replicas using token-validated Lua scripts:

```cpp
// Acquire lock for 5000ms, retry every 50ms up to 10 times
auto lock = co_await redis.lock(
    "lock:payout:user_123",
    std::chrono::milliseconds(5000),
    std::chrono::milliseconds(50),
    /*max_retries=*/10
);

if (!lock) {
    ctx.res().status(StatusCode::Conflict).text("Resource is currently locked");
    co_return;
}

// Critical section
// ... perform payment processing ...

// Optionally extend the lock lease
co_await lock->extend(std::chrono::milliseconds(3000));

// Explicit release (safe: only deletes key if token matches)
co_await lock->release();
```

> [!TIP]
> `RedisLock` uses RAII token tracking. When `release()` is called, it executes a Lua script comparing the stored token against the key value before deletion, preventing accidental release of locks acquired by another process after a timeout.

---

## Command Pipelining (`RedisPipeline`)

Pipelining packs dozens of Redis commands into a single TCP packet, avoiding per-command network round-trip delays (RTT):

```cpp
auto pipe = redis.pipeline();

pipe.set("user:1:name", "Alice")
    .incr("user:1:logins")
    .hset("user:1:meta", "ip", "10.0.0.1")
    .get("user:1:name");

// Dispatches single batch over io_uring
std::vector<RespValue> results = co_await pipe.execute();

for (const auto& res : results) {
    if (res.is_string()) {
        std::cout << "String: " << res.as_string() << "\n";
    } else if (res.is_integer()) {
        std::cout << "Integer: " << res.as_integer() << "\n";
    }
}
```

### Cluster Pipeline Support
In Redis Cluster mode, `RedisPipeline` automatically groups commands by their target shard pool, executes the sub-batches concurrently over the cluster nodes, and reconstructs the response vector in the original command order.

---

## Transactions (`MULTI` / `EXEC`)

To execute multiple commands atomically on a single Redis connection:

```cpp
auto opt_tx = co_await redis.multi();
if (!opt_tx) {
    throw std::runtime_error("Failed to acquire connection for MULTI");
}

auto& tx = *opt_tx;
tx.incr("counter:a")
  .incr("counter:b");

// Execute atomic transaction
std::vector<RespValue> tx_results = co_await tx.exec();

// Or discard if an error occurs
// co_await tx.discard();
```

---

## Pub/Sub Messaging

### Publishing

```cpp
co_await redis.publish("notifications:channel", R"({"type":"alert","level":"warn"})");
```

### Subscribing (`RedisSubscriber`)

```cpp
auto sub = redis.subscriber();
co_await sub.connect();

// Subscribe to exact channels or wildcard patterns
co_await sub.subscribe({"notifications:channel"});
co_await sub.psubscribe({"events:*"});

// Message loop in a background task
while (sub.is_connected()) {
    std::optional<RedisMessage> msg = co_await sub.next_message();
    if (!msg) break;

    std::cout << "Received on [" << msg->channel << "]: " << msg->payload << "\n";
}
```

---

## Redis Streams & Consumer Groups

Redis Streams enable event-driven architectures with consumer group acknowledgments:

```cpp
// 1. Append message to stream
std::string msg_id = co_await redis.xadd(
    "orders:stream",
    "*", // Auto-generate stream ID
    {{"order_id", "456"}, {"status", "placed"}}
);

// 2. Create consumer group (if not exists)
co_await redis.xgroup_create("orders:stream", "order_workers", "$", /*mkstream=*/true);

// 3. Read messages as a worker
auto read_results = co_await redis.xreadgroup(
    "order_workers", "worker_node_1",
    {"orders:stream"}, {">"},
    /*count=*/10,
    /*block_ms=*/std::chrono::milliseconds(2000)
);

for (const auto& stream : read_results) {
    for (const auto& message : stream.messages) {
        std::cout << "Message ID: " << message.id << "\n";
        // Acknowledge processed message
        co_await redis.xack("orders:stream", "order_workers", {message.id});
    }
}
```
