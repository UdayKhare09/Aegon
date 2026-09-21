# Transactions & Cache Consistency

Aegon provides atomic ACID transactions via `db.transaction()`. The `Transaction` object has **100% feature parity** with `SqlDatabaseClient`, allowing you to perform queries, updates, eager loading, relations, and batch operations within an isolated transaction boundary.

Critically, Aegon solves the classic "dirty cache on rollback" bug through **Deferred Cache Invalidation**.

---

## Transaction Lifecycle

Transactions are initiated via `db.transaction()` and accept a coroutine lambda taking `Transaction& tx`:

```cpp
co_await db.transaction([](Transaction& tx) -> Task<void> {
    // 1. Fetch user within transaction
    auto sender = co_await tx.find_by_id<Account>(1);
    auto receiver = co_await tx.find_by_id<Account>(2);

    if (sender->balance < 100.0) {
        // Throwing any exception automatically triggers ROLLBACK
        throw std::runtime_error("Insufficient funds");
    }

    // 2. Perform atomic updates
    sender->balance -= 100.0;
    receiver->balance += 100.0;

    co_await tx.update_entity(*sender);
    co_await tx.update_entity(*receiver);

    // 3. Normal return automatically commits
    co_return;
});
```

### Automatic Rollback on Exception
If any exception escapes the transaction lambda, `db.transaction()` intercepts it, issues `ROLLBACK` to the database, discards all uncommitted cache updates, and re-throws the exception to the caller.

---

## What Happens to the Cache During Transactions?

In traditional frameworks, updating a row immediately deletes or updates the Redis cache key. If the SQL transaction subsequently fails or rolls back, your cache becomes corrupt, holding uncommitted or invalid data.

Aegon uses **Deferred Transactional Cache Synchronization**:

```
                       [ Start Transaction ]
                                 │
                 tx.update_entity(order)
                                 │
                   SQL UPDATE executed in DB
                   Cache ops queued to memory:
                   [ DeferredCacheOp: Del order:1 ]
                   [ DeferredCacheOp: Incr orders:epoch ]
                                 │
                 ┌───────────────┴───────────────┐
             Success                           Failure
                  │                                 │
                  ▼                                 ▼
             [ COMMIT ]                        [ ROLLBACK ]
                  │                                 │
      Flush queued cache ops:              Discard queued ops!
      • Delete cache keys                  • Cache backend is NOT touched
      • Bump cache epochs                  • Cache remains pure
```

1. **During the Transaction**: Write operations (`insert`, `update_entity`, `delete_by_id`) record their cache invalidations into an in-memory `std::vector<DeferredCacheOp>`. No mutations are sent to the cache backend yet.
2. **On Successful `COMMIT`**: `db.flush_deferred_cache()` executes immediately after the SQL commit, applying all deferred invalidations and epoch increments atomically.
3. **On `ROLLBACK`**: The vector of deferred operations is simply discarded. The cache backend is untouched and remains 100% consistent with the database.

---

## Feature Parity: `Transaction` vs `SqlDatabaseClient`

`Transaction` mirrors the entire API surface of `SqlDatabaseClient`:

| Operation | `SqlDatabaseClient` Method | `Transaction` Equivalent |
| :--- | :--- | :--- |
| **Lookups** | `db.find_by_id<T>(id)` | `tx.find_by_id<T>(id)` |
| **Unique** | `db.find_by_unique(&T::f, val)`| `tx.find_by_unique(&T::f, val)` |
| **Queries** | `db.fetch_all(query)` | `tx.fetch_all(query)` |
| **Single Row**| `db.fetch_one(query)` | `tx.fetch_one(query)` |
| **Inserts** | `db.insert(entity)` | `tx.insert(entity)` |
| **Insert + ID**| `db.insert_get_id(entity)` | `tx.insert_get_id(entity)` |
| **Batch** | `db.insert_all(span<T>)` | `tx.insert_all(span<T>)` |
| **Updates** | `db.update_entity(entity)` | `tx.update_entity(entity)` |
| **Deletes** | `db.delete_by_id<T>(id)` | `tx.delete_by_id<T>(id)` |
| **Relations**| `db.load(relation)` | `tx.load(relation)` |
| **Links** | `db.link<Junction>(a, b)` | `tx.link<Junction>(a, b)` |
| **Aggregates**| `db.count(query)` | `tx.count(query)` |
| **Raw SQL** | `db.execute(sql, params)` | `tx.execute(sql, params)` |

---

## Manual Commit / Rollback

While RAII-style automatic commit is recommended, `Transaction` allows explicit control:

```cpp
co_await db.transaction([](Transaction& tx) -> Task<void> {
    co_await tx.execute("SAVEPOINT my_savepoint");

    // Perform partial work
    bool abort_early = check_condition();
    if (abort_early) {
        co_await tx.rollback();
        co_return;
    }

    co_await tx.commit();
    co_return;
});
```
