# Redis Topologies & Architecture

Aegon's Redis subsystem supports three deployment topologies out of the box: **Standalone**, **Sentinel High Availability**, and **Redis Cluster**.

For high-concurrency multi-threaded servers, Aegon provides **`PerCoreRedisClient`**, establishing lock-free, zero-contention socket pools pinned to each CPU core's `io_uring` ring.

---

## 1. Standalone Topology

Standard direct connection to a single Redis instance:

```cpp
#include <aegon/data/redis/RedisClient.h>

using namespace aegon::data::redis;

RedisNodeConfig config{
    .host = "127.0.0.1",
    .port = 6379,
    .password = "auth_token", // Optional
    .database = 0
};

// Create client with 16 connection slots in the pool
RedisClient redis(loop.ring(), config, /*pool_size=*/16);
```

---

## 2. Sentinel High Availability

Redis Sentinel provides automatic failover if the primary master crashes. Aegon queries Sentinel nodes to dynamically discover the current active master:

```cpp
SentinelConfig config{
    .master_name = "mymaster",
    .sentinels = {
        {"10.0.0.1", 26379},
        {"10.0.0.2", 26379},
        {"10.0.0.3", 26379}
    },
    .password = "redis_password",
    .database = 0
};

RedisClient redis(loop.ring(), config, /*pool_size=*/16);
```

Aegon's `RedisSentinelResolver` queries the sentinels using `SENTINEL get-master-addr-by-name <master_name>` and automatically handles primary reconnections.

---

## 3. Redis Cluster (Distributed Sharding)

Redis Cluster partitions keys across 16,384 hash slots. Aegon calculates CRC16 slot hashes (including `{hashtag}` support) and routes commands directly to the shard owning that slot:

```cpp
ClusterConfig config{
    .seed_nodes = {
        {"10.0.1.1", 7000},
        {"10.0.1.2", 7001},
        {"10.0.1.3", 7002}
    },
    .password = "cluster_password",
    .pool_size_per_node = 8
};

RedisClient redis(loop.ring(), config);
```

### How Slot Routing Works (`Crc16` & `RedisClusterRouter`)
- Commands are inspected for their routing key (e.g. `user:{100}:profile`).
- `crc16(std::string_view buf)`: Hardware-accelerated lookup table implementation of the XMODEM polynomial `0x1021`.
- `key_slot(std::string_view key)`: Computes the hash slot in `[0, 16383]`. Automatically parses `{hash_tag}` contents so co-located keys map to the same shard.
- Aegon dispatches the request to the dedicated connection pool for the node owning that slot.
- Slot topologies are initialized via `CLUSTER SLOTS` and dynamically refreshed upon `-MOVED` or `-ASK` redirects.


---

## Comparison Matrix: Server Topologies vs. PerCore

It is important to distinguish **server deployment topologies** (where Redis runs) from **`PerCoreRedisClient`** (how Aegon's multi-core server connects to Redis):

| Architecture | Role | Multi-Master Sharding | High Availability / Failover | Aegon Config Struct | How Aegon Connects |
| :--- | :--- | :---: | :---: | :--- | :--- |
| **Standalone** | Server Topology | ❌ No | ❌ No (Single node) | [`RedisNodeConfig`](#1-standalone-topology) | Connects directly to `host:port` |
| **Sentinel** | Server Topology | ❌ No (1 active master) | ✅ Yes (Automatic quorum failover) | [`SentinelConfig`](#2-sentinel-high-availability) | Dynamic master discovery via Sentinels |
| **Cluster** | Server Topology | ✅ Yes (16,384 hash slots) | ✅ Yes (Per-shard replica failover) | [`ClusterConfig`](#3-redis-cluster-distributed-sharding) | Client-side CRC16 slot routing |
| **`PerCore`** | **Client Connection Engine** | Supported (Wraps any topology) | Supported (Wraps any topology) | [`PerCoreRedisClient`](#4-percoreredisclient-zero-contention-multi-core-engine) | Dedicated socket pool pinned to each worker's `io_uring` ring |

> [!TIP]
> **`PerCoreRedisClient` is not an alternative to Cluster or Sentinel** — it is Aegon's zero-contention wrapper for *all* of them. You can run Standalone Per-Core, Sentinel Per-Core, or **Cluster Per-Core**.

---

## 4. `PerCoreRedisClient` (Zero-Contention Multi-Core Engine)

In an Aegon multi-threaded HTTP server (`server.run(threads)`), each worker thread runs its own isolated `EventLoop` and `io_uring` ring. Sharing a single Redis client with mutexes causes lock contention, cache-line bouncing, and cross-ring synchronization penalties.

`PerCoreRedisClient` provides a thread-affinity container that lazily instantiates and binds a private `RedisClient` to the calling worker's `EventLoop::current()->ring()`.

```
                  PerCoreRedisClient
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
   [ Core 0 ]       [ Core 1 ]       [ Core 2 ]
   Thread-local     Thread-local     Thread-local
   Redis Client     Redis Client     Redis Client
        │                │                │
        ▼                ▼                ▼
   Ring 0 I/O       Ring 1 I/O       Ring 2 I/O
```

`PerCoreRedisClient` accepts any configuration:
```cpp
// Standalone per-core:
PerCoreRedisClient(RedisNodeConfig node_cfg, size_t pool_size = 8);

// Sentinel per-core:
PerCoreRedisClient(SentinelConfig sentinel_cfg, size_t pool_size = 8);

// Cluster per-core:
PerCoreRedisClient(ClusterConfig cluster_cfg);
```

---

## 5. Using Redis Cluster in Aegon HTTP `Server`

To use a distributed **Redis Cluster** across all server threads without mutex contention, pass `ClusterConfig` directly into `PerCoreRedisClient` and register it with `server.provide()`:

```cpp
#include <aegon/Server.h>
#include <aegon/data/redis/PerCoreRedisClient.h>

using namespace aegon;
using namespace aegon::data::redis;

int main() {
    Server server;

    // 1. Define Cluster configuration
    ClusterConfig cluster_cfg{
        .seed_nodes = {
            {"10.0.1.10", 7000},
            {"10.0.1.11", 7001},
            {"10.0.1.12", 7002}
        },
        .password = "cluster_auth_secret",
        .pool_size_per_node = 8 // Connections per cluster shard on each CPU core
    };

    // 2. Wrap in PerCoreRedisClient (Thread-Affinity Cluster)
    auto per_core_cluster = std::make_shared<PerCoreRedisClient>(cluster_cfg);

    // 3. Register as a server service
    server.provide<PerCoreRedisClient>(per_core_cluster);

    // 4. Access inside route handlers on any worker thread
    server.router().get("/user/:id/session", [](Context& ctx) -> Task<void> {
        auto& redis = ctx.service<PerCoreRedisClient>();
        std::string user_id = ctx.param("id");

        // The key is hashed via CRC16 and routed directly to the cluster shard
        // using the calling worker thread's local io_uring ring!
        auto session = co_await redis->get("user:{" + user_id + "}:session");

        if (!session) {
            ctx.res().status(404).json({{"error", "Session not found"}});
            co_return;
        }

        ctx.res().json({{"session", *session}});
        co_return;
    });

    server.router().post("/user/:id/session", [](Context& ctx) -> Task<void> {
        auto& redis = ctx.service<PerCoreRedisClient>();
        std::string user_id = ctx.param("id");
        std::string token = ctx.req().body();

        // Expire session in 3600 seconds
        co_await redis->set("user:{" + user_id + "}:session", token, 3600);

        ctx.res().status(201).json({{"status", "created"}});
        co_return;
    });

    // Run across all available CPU cores
    server.listen(8080);
    server.run(8);
    return 0;
}
```

### Redis Cluster Considerations
- **Hash Tags `{...}`**: When running multi-key operations or transactions (`MGET`, `MSET`, multi-key Lua scripts) on Redis Cluster, use hash tags like `{user:123}:session` and `{user:123}:profile` to guarantee all keys map to the exact same hash slot (`CRC16({tag}) % 16384`).
- **Connection Count**: Because each core opens `pool_size_per_node` connections to each cluster shard, total connections = `num_cores × pool_size_per_node × num_shards`. Size `pool_size_per_node` accordingly (e.g. 4–8 connections per core is typically optimal for async non-blocking I/O).
