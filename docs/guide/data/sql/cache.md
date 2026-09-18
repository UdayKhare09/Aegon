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

## Declarative Schema Cache Configuration

Caching behavior is declared directly in the entity's compile-time `TableDef` schema definition:

```cpp
#include <aegon/data/orm/sql/Table.h>

using namespace aegon::data::orm::sql;

struct User {
    int64_t id{0};
    std::string email;
    std::string username;
    int64_t organization_id{0};

    static auto schema() {
        return TableDef<User>("users")
            .id(&User::id, "id")
            .column(&User::email, "email").unique()
            .column(&User::username, "username")
            .column(&User::organization_id, "organization_id")
            // 1. Enable primary key caching with a 600-second TTL
            .cache_by_id(std::chrono::seconds(600))
            // 2. Set up unique pointer for lookups by email
            .by_unique(&User::email)
            // 3. Partition query invalidations by organization
            .partition_by(&User::organization_id)
            // 4. Invalidation mode: Partitioned
            .invalidation_mode(InvalidationMode::Partitioned)
            // 5. Mutation synchronization policy
            .mutation_sync(MutationSync::EvictOnWrite);
    }
};
```

### Schema Configuration API

| Method / Option | Parameter / Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `.cache_by_id(ttl)` | `std::chrono::seconds` | `300s` | Enables normalized entity row caching under `<table_name>:id:<pk>`. |
| `.by_unique(&Entity::field)` | Member Pointer | None | Sets up unique-column alias pointers `<table_name>:<col>:<val>` pointing to primary key `<pk>`. |
| `.partition_by(&Entity::field)` | Member Pointer | None | Scopes query epoch invalidations to parent/tenant IDs instead of evicting the whole table. Automatically sets `InvalidationMode::Partitioned`. |
| `.invalidation_mode(mode)` | `InvalidationMode` | `StrictEpoch` | Determines how mutations bump version epochs (`StrictEpoch`, `Partitioned`, `PredicateAware`, `TtlOnly`). |
| `.mutation_sync(sync)` | `MutationSync` | `EvictOnWrite` | Controls whether `update_entity` purges the entity key (`EvictOnWrite`) or overwrites it with new serialized data (`UpdateOnWrite`). |

---

## Two-Phase Query Pointer Execution Workflow

To avoid cache bloat and stale duplicates across overlapping queries, queries cache **lists of primary key IDs**, not duplicated entity blobs.

```
Incoming Cached Query
         │
         ▼
[ Step 1: Compute Query Fingerprint ]
  SHA256( Dialect SQL + Bound Parameters + Active Table/Partition/Predicate Epoch )
         │
         ▼
[ Step 2: Check Query Pointer Key ]
  cache.get("users:q:v2:<fingerprint>")
         │
    ┌────┴────────────────────────┐
    ▼ (Hit)                       ▼ (Miss)
[ Read ID List: "1,42,108" ]    [ Execute SQL in Database ]
    │                             │
    ▼                             ▼
[ Pipelined MGET ]              [ Extract Primary Keys ]
  MGET users:id:1 ...             │
    │                             ▼
    ▼                           [ Store Query Pointer Key ]
[ Glaze JSON Deserialization ]    SET users:q:v2:<fp> "1,42,108" EX 600
    │                             │
    │ (If any entity missing)     ▼
    └────────► Re-fetch holes   [ Pipelined MSET Entity Rows ]
               from SQL           MSET users:id:1 {...} users:id:42 {...}
```

1. **Deterministic Fingerprinting**: Aegon normalizes the generated SQL query, serializes parameters, and appends the active version epoch.
2. **Cache Hit**: Retrieves the comma-separated ID string (e.g. `"1,42,108"`), then issues a single pipelined `MGET` for `users:id:1`, `users:id:42`, and `users:id:108`.
3. **Automatic Hole Healing**: If an individual entity was evicted while the query pointer remained, Aegon automatically detects the missing key, queries only the missing row from the database, and heals the entity cache.
4. **Cache Miss**: Executes the query against PostgreSQL / SQLite, records the primary keys into the query pointer key with the configured TTL, and saves the entity JSON representations in Redis via pipelined `MSET`.

---

## Automated Mutation Invalidation

All mutations executed through `SqlDatabaseClient` orchestrate cache updates automatically:

| Operation | Entity Cache Action | Query Cache Action |
| :--- | :--- | :--- |
| `co_await db.insert(entity)` | Populates `<table_name>:id:<new_id>` | Increments `<table_name>:epoch` (or partition/predicate epoch), instantly invalidating all cached query pointers in $O(1)$. |
| `co_await db.update_entity(entity)` | **`EvictOnWrite`**: Deletes `<table_name>:id:<pk>`.<br>**`UpdateOnWrite`**: Overwrites `<table_name>:id:<pk>`. | Increments target epoch counter, invalidating affected query caches. |
| `co_await db.delete_by_id<T>(id)` | Purges `<table_name>:id:<id>` and unique alias pointer. | Increments target epoch counter. |
| `co_await db.insert_all(span)` | Batch-inserts entities and warms entity keys. | Atomically increments epoch counter once for the entire batch. |

---

## The `CacheBackend` Interface

If you wish to implement a custom cache backend (such as Memcached, Dragonfly, or a custom distributed storage layer), implement the `CacheBackend` abstract interface:

```cpp
#include <aegon/data/cache/CacheBackend.h>

class CacheBackend {
public:
    virtual ~CacheBackend() = default;

    virtual core::Task<std::optional<std::string>> get(std::string_view key) = 0;
    virtual core::Task<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys) = 0;
    virtual core::Task<bool> set(std::string_view key, std::string_view val, std::optional<std::chrono::seconds> ttl = std::nullopt) = 0;
    virtual core::Task<bool> del(std::string_view key) = 0;
    virtual core::Task<int64_t> del_many(const std::vector<std::string>& keys) = 0;
    virtual core::Task<int64_t> incr(std::string_view key) = 0;
};
```

Both `RedisCacheBackend` and `InMemoryCacheBackend` implement this interface with full thread-safety and zero-copy string views.

---

## Transactional Cache Safety

As detailed in [Transactions & Consistency](/guide/data/sql/transactions), mutations inside `db.transaction()` buffer their cache invalidations in memory until the SQL transaction commits successfully. If the transaction rolls back, all cache invalidations are discarded, preventing cache contamination.

