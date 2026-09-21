# MemStore — Async In-Process Cache

Aegon ships `MemStore` as a zero-dependency, async in-process alternative to Redis. It runs on its own dedicated thread (like Redis's single-threaded event loop) and exposes a fully async API: HTTP worker coroutines post requests and `co_await` results via Linux `io_uring` eventfd reads — the event loop is never blocked.

Use `MemStore` when:
- You want caching without running an external Redis server.
- You are building a single-node service, embedding Aegon, or writing tests.
- You need deterministic local cache semantics (no network round-trips, no serialization).

For distributed multi-node deployments, use [`RedisCacheBackend`](/guide/data/sql/cache) instead.

---

## Setup

```cpp
#include <aegon/data/memory/MemStore.h>
#include <aegon/data/memory/MemStoreCacheBackend.h>

using namespace aegon::data::memory;
```

### CMake target

```cmake
target_link_libraries(my_app PRIVATE Aegon::memory)
```

---

## Instantiation & Server Registration

```cpp
// 1. Create the MemStore with optional configuration
//    The background thread starts automatically — no start() needed.
auto store = std::make_shared<MemStore>(MemStoreConfig{
    .max_entries    = 500'000,
    .sweep_interval = std::chrono::milliseconds(500)
});

// 2. Register as a server service
server.provide<MemStore>(store);

// 3. (Optional) attach as the SQL ORM cache backend
auto mem_cache = std::make_shared<MemStoreCacheBackend>(store);
db->set_cache(mem_cache);
```

> [!NOTE]
> `MemStore` starts its background thread in the constructor and stops it in the destructor. No manual lifecycle calls required.

---

## Configuration (`MemStoreConfig`)

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `max_entries` | `size_t` | `1,000,000` | Maximum number of live keys. When full, the least-recently-used key is evicted before inserting a new one. |
| `sweep_interval` | `std::chrono::milliseconds` | `500ms` | How often the MemStore thread scans for and purges TTL-expired keys. |

---

## Core API

All operations below are usable directly on `MemStore` via `MemStoreCacheBackend`, or by posting `MemRequest` structs manually for advanced use cases.

### Get

```cpp
// Returns std::nullopt on cache miss or TTL expiry
std::optional<std::string> val = co_await cache.get("user:session:100");

if (val) {
    std::cout << "Hit: " << *val << "\n";
} else {
    std::cout << "Miss\n";
}
```

### Set (with optional TTL)

```cpp
// Persistent (no TTL)
co_await cache.set("config:feature_flags", serialized_flags);

// With TTL — auto-evicted after 1 hour
co_await cache.set("user:session:100", "active", std::chrono::seconds(3600));
```

### Delete

```cpp
bool was_present = co_await cache.del("user:session:100");
```

### Multi-Get

```cpp
std::vector<std::optional<std::string>> values =
    co_await cache.mget({"k1", "k2", "k3"});

for (size_t i = 0; i < values.size(); ++i) {
    if (values[i]) std::cout << "k" << i << " = " << *values[i] << "\n";
    else           std::cout << "k" << i << " = <miss>\n";
}
```

### Multi-Delete

```cpp
// Returns the number of keys that existed and were deleted
int64_t deleted_count = co_await cache.del_many({"sess:1", "sess:2", "sess:42"});
```

### Atomic Increment

```cpp
// Atomically increments an integer key. Creates it at 1 if it doesn't exist.
int64_t views = co_await cache.incr("metrics:page_views");
```

> [!NOTE]
> All operations run exclusively on the MemStore thread — reads, writes, increments, and deletes are all serialized through the same event queue. There are no data races and no locks on the hot-path map.

---

## TTL Expiry

TTLs are set per-key at write time:

```cpp
co_await cache.set("rate_limit:user:42", "10", std::chrono::seconds(60));
```

Expired keys are lazily evicted on `get` (if accessed after expiry) and proactively swept in bulk every `sweep_interval` milliseconds by the MemStore thread. This mirrors Redis's combined lazy + active expiry strategy.

---

## LRU Eviction

When the store reaches `max_entries`, the **least-recently-used key is evicted** before the new entry is inserted. Both reads (`get`) and writes (`set`) count as a "use" and move the key to the front of the LRU list.

```cpp
// Low-memory embedded config: cap at 10,000 entries
auto store = std::make_shared<MemStore>(MemStoreConfig{
    .max_entries = 10'000,
});
```

---

## Using as ORM Cache Backend

`MemStoreCacheBackend` implements the `CacheBackend` interface and is a drop-in replacement for `RedisCacheBackend` when used with `SqlDatabaseClient`:

```cpp
#include <aegon/data/memory/MemStoreCacheBackend.h>

// In server on_start or main:
auto mem_cache = std::make_shared<MemStoreCacheBackend>(store);
db->set_cache(mem_cache);
```

The ORM's normalized pointer caching, query epoch invalidation, and deferred transactional cache ops all work identically with `MemStoreCacheBackend`. See [ORM Cache](/guide/data/sql/cache) for full details.

---

## Monitoring & Stats

```cpp
// Live key count (approximate — read from the MemStore thread's map)
size_t count = store->size();

// Hit / miss counters (atomic — safe to read from any thread)
uint64_t hits   = store->hits();
uint64_t misses = store->misses();

double hit_rate = static_cast<double>(hits) / (hits + misses) * 100.0;
std::println("Cache hit rate: {:.1f}%", hit_rate);

// Reset counters
store->reset_stats();
```

---

## Architecture

```
HTTP Worker Threads                    MemStore Thread
──────────────────                     ───────────────
co_await cache.get("key")
  │
  ├─ create eventfd (O_NONBLOCK)
  ├─ push MemRequest* to queue  ──────► drain request queue
  ├─ io_uring_prep_read(efd)           process all ops on map
  └─ coroutine suspends                write results to req struct
                                ◄────── write(efd, 1)
  io_uring wakes coroutine             ↓
  read result from req struct          sweep TTL-expired keys
  close(efd)                           evict LRU if over capacity
  co_return result
```

**Key properties:**

| Property | Detail |
| :--- | :--- |
| **Thread model** | One dedicated thread owns the map — no data locking needed |
| **Worker suspension** | `io_uring` eventfd read — event loop is never blocked |
| **Queue contention** | Mutex held for ~100ns per enqueue (pointer push + notify) |
| **TTL strategy** | Lazy eviction on `get` + proactive background sweep |
| **Eviction policy** | LRU (doubly-linked list + hash map, O(1) per operation) |
| **Per-core variant** | Not needed — no sockets, no rings, no network overhead |

---

## Comparison: MemStore vs Redis

| | **MemStore** | **Redis** |
| :--- | :--- | :--- |
| Deployment | In-process, zero setup | External server required |
| Network latency | None | ~0.1–1ms per op |
| Data persistence | None (process lifetime) | RDB / AOF snapshots |
| Data sharing | Single process only | Any number of services |
| Max capacity | RAM of the host process | Configurable RAM + disk |
| Operations | `get`, `mget`, `set`, `del`, `del_many`, `incr` | Full Redis command set |
| Best for | Single-node, testing, embedded | Production, distributed |
