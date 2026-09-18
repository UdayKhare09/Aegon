# Data Mutations

Aegon provides type-safe methods for inserting, updating, and deleting records. All mutations execute on the primary connection pool, enforce Optimistic Concurrency Control (OCC), and trigger automated cache invalidation.

---

## Inserting Entities

### Single Entity Insertion (`db.insert`)

```cpp
User user{
    .username = "marcus",
    .email = "marcus@rome.gov",
    .bio = "Emperor & Philosopher"
};

// Executes INSERT INTO users (...) VALUES (...)
// If the primary key is auto-increment, user.id is automatically populated!
co_await db.insert(user);

std::cout << "Created user with generated ID: " << user.id << "\n";
```

### Retrieving Generated ID Explicitly (`db.insert_get_id`)

```cpp
int64_t new_id = co_await db.insert_get_id(user);
```

### Batch Insertion (`db.insert_all`)

Insert multiple records in a single database round-trip:

```cpp
std::vector<User> batch = {
    {.username = "user1", .email = "u1@test.com"},
    {.username = "user2", .email = "u2@test.com"},
    {.username = "user3", .email = "u3@test.com"}
};

co_await db.insert_all(std::span<User>(batch));
```

---

## Updating Entities

### Full Entity Update (`db.update_entity`)

Updates all columns of an existing entity matching its primary key:

```cpp
auto opt_user = co_await db.find_by_id<User>(1);
if (opt_user) {
    opt_user->bio = "Updated biography";

    // Executes UPDATE users SET ... WHERE id = 1
    size_t rows_affected = co_await db.update_entity(*opt_user);
}
```

#### Optimistic Concurrency Protection
If your schema defines `.version(&Entity::version)`, `update_entity` automatically executes:

```sql
UPDATE users SET bio = $1, version = version + 1 WHERE id = $2 AND version = $3;
```

If another thread updated the record in the meantime, `rows_affected == 0` and Aegon throws `aegon::data::orm::sql::OptimisticLockException`.

---

### Partial Updates (`UpdateBuilder`)

To update specific fields across multiple rows without fetching full entities into memory, use `db.update<Entity>()`:

```cpp
auto update_query = db.update<User>()
    .set(&User::is_active, false)
    .where(&User::last_login, Op::Lt, "2025-01-01")
    .to_sql(db.primary_pool().dialect());

size_t affected = co_await db.execute(update_query);
```

---

## Deleting Entities

### Delete by Primary Key (`db.delete_by_id`)

```cpp
// Returns Task<bool> indicating if a row was deleted
bool deleted = co_await db.delete_by_id<User>(42);

if (deleted) {
    std::cout << "User 42 removed.\n";
}
```

### Batch Deletion (`DeleteBuilder`)

Delete rows matching arbitrary criteria:

```cpp
auto delete_query = db.delete_from<Session>()
    .where(&Session::expires_at, Op::Lt, DateTime::now())
    .to_sql(db.primary_pool().dialect());

size_t purged_count = co_await db.execute(delete_query);
```

---

## Mutations & Automated Cache Invalidation

When a cache backend (e.g. Redis or In-Memory) is attached to `SqlDatabaseClient`:

1. **`insert()`**: Automatically increments the table epoch (`table:epoch`), invalidating cached `SelectBuilder` queries that observe new rows.
2. **`update_entity()`**: Evicts or rewrites the primary key cache key (`table:id:<id>`) and unique pointer keys, while bumping the query epoch.
3. **`delete_by_id()`**: Immediately purges the entity from cache and bumps the table epoch.
