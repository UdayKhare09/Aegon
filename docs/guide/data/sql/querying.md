# Querying Data

Aegon's query engine generates parameterized SQL queries for PostgreSQL and SQLite. It provides type-safe member pointer expressions, read-replica load balancing, and built-in query caching.

---

## Primary Key & Unique Lookups

For point lookups, `SqlDatabaseClient` provides high-speed shorthands:

### Find by Primary Key

```cpp
// Returns Task<std::optional<User>>
auto user = co_await db.find_by_id<User>(42);

if (user) {
    std::cout << "Found: " << user->username << "\n";
}
```

### Find by Unique Field

```cpp
// Look up by unique column (&User::email)
auto user = co_await db.find_by_unique(&User::email, "alex@example.com");
```

> [!TIP]
> If caching is enabled on the schema (`.cache_by_id()` and `.by_unique()`), these methods transparently check and populate the cache backend (e.g. Redis or In-Memory) before hitting SQL.

---

## Fluent Query Builder (`SelectBuilder`)

Initialize a query using `db.from<Entity>()` or `aegon::data::orm::sql::from<Entity>()`:

```cpp
auto query = db.from<User>()
    .where(&User::is_active, Op::Eq, true)
    .order_by(&User::created_at, SortOrder::Desc)
    .limit(10);

std::vector<User> users = co_await db.fetch_all(query);
```

### Filtering with `.where()`

```cpp
// Equality & comparisons
query.where(&User::age, Op::Gte, 21);
query.and_where(&User::role, Op::Eq, "admin");
query.or_where(&User::is_superadmin, Op::Eq, true);

// String matching (LIKE / NOT LIKE)
query.where(&User::email, Op::Like, "%@company.com");

// Set membership (IN / NOT IN)
std::vector<std::string> roles = {"admin", "editor", "moderator"};
query.where_in(&User::role, roles);

// Range filtering (BETWEEN)
query.where_between(&User::age, 18, 65);

// Null checks
query.where_null(&User::deleted_at);
query.where_not_null(&User::bio);
```

#### Comparison Operators (`Op` Enum)
- `Op::Eq` (`=`)
- `Op::Neq` (`!=`)
- `Op::Gt` (`>`), `Op::Gte` (`>=`)
- `Op::Lt` (`<`), `Op::Lte` (`<=`)
- `Op::Like` (`LIKE`), `Op::NotLike` (`NOT LIKE`)
- `Op::In` (`IN`), `Op::NotIn` (`NOT IN`)
- `Op::IsNull` (`IS NULL`), `Op::IsNotNull` (`IS NOT NULL`)
- `Op::Between` (`BETWEEN`)

---

## Sorting, Grouping & Pagination

```cpp
auto q = db.from<Product>()
    .where(&Product::category, Op::Eq, "electronics")
    .order_by(&Product::price, SortOrder::Asc)
    .order_by(&Product::created_at, SortOrder::Desc)
    .limit(20)
    .offset(40);
```

### Automatic Pagination (`db.paginate`)

Instead of calculating offsets manually, use `db.paginate()`:

```cpp
// Page 2, 20 items per page
Page<Product> page = co_await db.paginate(q, 2, 20);

std::cout << "Current Page: " << page.current_page << "\n";
std::cout << "Total Items:  " << page.total_items << "\n";
std::cout << "Total Pages:  " << page.total_pages << "\n";
std::cout << "Has Next:     " << std::boolalpha << page.has_next << "\n";

for (const auto& item : page.items) {
    std::cout << " - " << item.name << " ($" << item.price << ")\n";
}
```

---

## Aggregations & Counts

Execute server-side SQL aggregate functions directly:

```cpp
// Count matching rows
uint64_t total_users = co_await db.count(
    db.from<User>().where(&User::is_active, Op::Eq, true)
);

// Sum
std::optional<double> revenue = co_await db.sum(
    db.from<Order>().where(&Order::status, Op::Eq, "completed"),
    &Order::total_amount
);

// Average
std::optional<double> avg_age = co_await db.avg(db.from<User>(), &User::age);

// Min and Max
std::optional<int> min_score = co_await db.min(db.from<Player>(), &Player::score);
std::optional<int> max_score = co_await db.max(db.from<Player>(), &Player::score);
```

---

## Execution Methods Summary

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `co_await db.fetch_all(query)` | `Task<std::vector<T>>` | Fetches all rows matching query. |
| `co_await db.fetch_one(query)` | `Task<std::optional<T>>` | Appends `LIMIT 1` and returns first matching entity. |
| `co_await db.count(query)` | `Task<uint64_t>` | Executes `SELECT COUNT(*) ...`. |
| `co_await db.sum(query, &T::f)` | `Task<std::optional<Val>>` | Executes `SELECT SUM(col) ...`. |
| `co_await db.avg(query, &T::f)` | `Task<std::optional<double>>` | Executes `SELECT AVG(col) ...`. |
| `co_await db.min(query, &T::f)` | `Task<std::optional<Val>>` | Executes `SELECT MIN(col) ...`. |
| `co_await db.max(query, &T::f)` | `Task<std::optional<Val>>` | Executes `SELECT MAX(col) ...`. |
| `co_await db.paginate(q, p, sz)`| `Task<Page<T>>` | Executes count + limit/offset query returning metadata. |
