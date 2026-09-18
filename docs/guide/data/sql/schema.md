# ORM Schema Definition

Aegon's Compile-Time Object-Relational Mapper (ORM) eliminates runtime reflection, code generation tools, and fragile macros. 

Schemas are defined directly in C++ using fluent member-pointer descriptors (`&Entity::member`). The compiler validates all column types, offsets, and foreign key references at compile time.

---

## Defining an Entity

To map a C++ struct to a database table, implement a static `schema()` method returning `aegon::data::orm::sql::TableDef<Entity>`:

```cpp
#include <aegon/data/orm/sql/Sql.h>

using namespace aegon::data::orm::sql;
using aegon::data::types::DateTime;

struct User {
    uint64_t id{0};
    std::string username;
    std::string email;
    std::optional<std::string> bio;
    bool is_active{true};
    int64_t version{1}; // For Optimistic Concurrency Control
    DateTime created_at;
    DateTime updated_at;

    static auto schema() {
        return TableDef<User>("users")
            .id(&User::id, "id")
            .column(&User::username, "username").unique().not_null().length(50)
            .column(&User::email, "email").unique().not_null().length(255)
            .column(&User::bio, "bio").nullable()
            .column(&User::is_active, "is_active").default_value(true)
            .version(&User::version, "version")
            .created_at(&User::created_at)
            .updated_at(&User::updated_at);
    }
};
```

---

## Primary Keys (`.id`)

The `.id()` method marks a field as the primary key:

```cpp
table.id(&User::id, "id");
```

- **Auto-Increment**: Integral fields (`uint64_t`, `int32_t`, etc.) are automatically treated as auto-incrementing identity columns (`SERIAL` / `BIGSERIAL` in Postgres, `INTEGER PRIMARY KEY AUTOINCREMENT` in SQLite).
- Generated IDs are automatically populated on the entity struct upon `insert()`.

---

## Column Attributes & Modifiers

Column modifiers are chained fluently onto `.column()`:

| Modifier | Description | Generated SQL Example |
| :--- | :--- | :--- |
| `.not_null()` | Disallows `NULL` values. (Default for non-`std::optional` fields) | `NOT NULL` |
| `.nullable()` | Allows `NULL` values. (Default for `std::optional<T>`) | `NULL` |
| `.unique()` | Creates a unique constraint on the column. | `UNIQUE` |
| `.indexed()` | Generates a secondary index for fast querying. | `CREATE INDEX ...` |
| `.length(N)` | Restricts string width to `VARCHAR(N)`. | `VARCHAR(50)` |
| `.default_value(val)` | Specifies column default value in the database. | `DEFAULT true` / `DEFAULT 'draft'` |

---

## Foreign Keys & Cascading Deletes

Establish referential integrity between tables using `.references()`:

```cpp
struct Post {
    uint64_t id{0};
    uint64_t author_id{0};
    std::string title;
    std::string content;

    static auto schema() {
        return TableDef<Post>("posts")
            .id(&Post::id, "id")
            .column(&Post::author_id, "author_id")
                .references(&User::id)
                .on_delete_cascade()
                .on_update_cascade()
            .column(&Post::title, "title").not_null().length(200)
            .column(&Post::content, "content").not_null();
    }
};
```

### Supported Delete Actions
- `.on_delete_cascade()` — Deletes dependent records when parent is deleted.
- `.on_delete_set_null()` — Sets foreign key to `NULL` on parent deletion (requires `.nullable()`).
- `.on_delete_restrict()` — Rejects parent deletion if dependent rows exist.

---

## Automatic Timestamps

Aegon manages audit timestamps automatically during `insert()` and `update()` operations:

```cpp
table.created_at(&User::created_at, "created_at");
table.updated_at(&User::updated_at, "updated_at");
```

- `created_at` is set to the current UTC timestamp during initial entity insertion.
- `updated_at` is automatically bumped to the current UTC timestamp whenever the entity is updated.

---

## Optimistic Concurrency Control (OCC)

Protect against race conditions and concurrent write hazards without database table locks by registering a version column:

```cpp
table.version(&Product::version, "version");
```

When updating an entity, Aegon appends `WHERE version = :current_version` and increments `version = version + 1`. If another transaction modified the row concurrently, an `OptimisticLockException` is thrown.

---

## Smart Cache Configuration

Entity caching policies can be declared directly on the schema:

```cpp
static auto schema() {
    return TableDef<User>("users")
        .id(&User::id)
        .column(&User::email).unique()
        .cache_by_id(std::chrono::seconds(300)) // Cache entities for 5 minutes
        .by_unique(&User::email)                // Secondary pointer cache by unique email
        .invalidation_mode(InvalidationMode::StrictEpoch)
        .mutation_sync(MutationSync::EvictOnWrite);
}
```

- **`cache_by_id(ttl)`**: Automatically caches `find_by_id` lookups.
- **`by_unique(&Entity::field)`**: Maintains a pointer index from unique column to primary key.
- **`invalidation_mode`**: Controls how cache invalidation behaves during writes (`StrictEpoch`, `Partitioned`, `TtlOnly`).
- **`mutation_sync`**: `EvictOnWrite` (deletes cache key on update) or `UpdateOnWrite` (rewrites cached value).
