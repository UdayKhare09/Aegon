# Query & Entity Caching

Aegon features a zero-allocation query and entity caching layer tightly integrated with both the compile-time ORM and the Redis/In-Memory cache backends.

It solves the two most notorious problems in database caching:
1. **Duplicate Entity Caching**: Multiple queries returning the same entity store only normalized primary key pointers, reducing cache memory consumption by up to 80%.
2. **Invalidation Overhead & Thundering Herds**: Uses $O(1)$ Epoch Tags to invalidate entire query result sets without slow `KEYS` or `SCAN` operations.

---

## Cache Backends

Aegon decouples cache storage from cache semantics via the `CacheBackend` interface:

```cpp
#include <aegon/data/cache/RedisCacheBackend.h>
#include <aegon/data/cache/InMemoryCacheBackend.h>

using namespace aegon::data::cache;

// 1. Redis Cache Backend (Cluster / Sentinel / Standalone / PerCore)
auto redis_cache = std::make_shared<RedisCacheBackend>(per_core_redis->provider(), "cache:");

// 2. In-Memory Local Cache Backend (for single-node / testing)
auto memory_cache = std::make_shared<InMemoryCacheBackend>();

// Attach cache backend to SQL Database Client
db->set_cache(redis_cache);
```

---

## Normalized Entity & Pointer Caching

In typical naive caching, `SELECT * FROM users WHERE active = true` and `SELECT * FROM users WHERE role = 'admin'` store two separate copies of User #1 in cache. When User #1 is updated, one query cache might be invalidated while the other remains stale.

Aegon uses **Normalized Pointer Caching**:

```
[ Query Cache ]
users:q:v1:hash_123 ──► "1,42,108"  (Comma-separated PK IDs)

[ Normalized Entity Cache ]
users:id:1          ──► {"id":1,"name":"Alice"}
users:id:42         ──► {"id":42,"name":"Bob"}
users:id:108        ──► {"id":108,"name":"Charlie"}

[ Secondary Unique Pointer ]
users:email:alice@x.com ──► "1"
```

1. Queries cache only a list of primary keys (`"1,42,108"`).
2. Aegon fetches entities in a single batch using `MGET` on their primary key keys.
3. Updating User #1 immediately reflects across all queries referencing User #1 without cache desynchronization.

---

## Invalidation Strategies (`InvalidationMode`)

Declared on table schemas via `.invalidation_mode()`:

### 1. `StrictEpoch` (Default)
Increments an atomic table version counter (`table:epoch`) on any insert or delete. 

- Query fingerprints incorporate this epoch: `users:q:v<epoch>:<hash>`.
- Any mutation bumps the epoch from `v1` to `v2`.
- Previous query cache entries automatically become unreachable and naturally expire via TTL without executing a single `DEL` scan!

### 2. `Partitioned`
Scopes query invalidation to a parent foreign key or multi-tenant ID (e.g., `tenant_id` or `organization_id`):

```cpp
table.partition_by(&Project::organization_id);
```

Mutations in Organization A only increment `projects:part:OrgA:epoch`. Cached queries for Organization B remain completely untouched!

### 3. `PredicateAware`
Tracks column-level equality values (e.g. `category=shoes`) and increments targeted predicate epochs (`table:pred:category:shoes:epoch`).

### 4. `TtlOnly`
Writes never invalidate query results; cache entries decay purely based on configured TTL. Ideal for analytics dashboards, leaderboards, and high-frequency sensor telemetry.

---

## Query Caching in Action

```cpp
// 1. Point lookup (transparently checks users:id:42)
auto user = co_await db.find_by_id<User>(42);

// 2. Unique column lookup (checks pointer users:email:alex@x.com -> users:id:42)
auto user2 = co_await db.find_by_unique(&User::email, "alex@x.com");

// 3. Complex query caching with custom TTL
auto q = db.from<Product>()
    .where(&Product::category, Op::Eq, "books")
    .cached(std::chrono::seconds(600));

std::vector<Product> products = co_await db.fetch_all(q);
```

---

## Transactional Cache Safety

As detailed in [Transactions & Consistency](/guide/data/sql/transactions), mutations inside `db.transaction()` buffer their cache invalidations in memory until the SQL transaction commits successfully. If the transaction rolls back, all cache invalidations are discarded, preventing cache contamination.
