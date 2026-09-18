# Relationships & Associations

Aegon supports first-class relational modeling with zero reflection: **One-to-One (`HasOne<T>`)**, **One-to-Many (`HasMany<T>`)**, and **Many-to-Many (`HasMany<T>` through a junction table)**.

Relations support both high-speed **batch eager loading** (`.include()`) that prevents N+1 query problems, and asynchronous **lazy loading** (`.load()`).

---

## Defining Relationships in Schemas

Declare relational fields on your entity structs using `HasOne<T>` or `HasMany<T>`:

```cpp
#include <aegon/data/orm/sql/Sql.h>

using namespace aegon::data::orm::sql;

struct Profile {
    uint64_t id{0};
    uint64_t user_id{0};
    std::string website;

    static auto schema() {
        return TableDef<Profile>("profiles")
            .id(&Profile::id)
            .column(&Profile::user_id).references(&User::id)
            .column(&Profile::website);
    }
};

struct Post {
    uint64_t id{0};
    uint64_t author_id{0};
    std::string title;

    static auto schema() {
        return TableDef<Post>("posts")
            .id(&Post::id)
            .column(&Post::author_id).references(&User::id)
            .column(&Post::title);
    }
};

struct User {
    uint64_t id{0};
    std::string username;

    // Relational members:
    HasOne<Profile> profile;
    HasMany<Post> posts;

    static auto schema() {
        return TableDef<User>("users")
            .id(&User::id)
            .column(&User::username)
            .has_one(&User::profile, &Profile::user_id)
            .has_many(&User::posts, &Post::author_id);
    }
};
```

---

## Eager Loading (`.include`)

The `.include()` clause eliminates N+1 query overhead by executing a single secondary batch query (`WHERE foreign_key IN (...)`) and automatically hydrating child entities across all parent records in memory.

### Unscoped Eager Loading

```cpp
auto query = db.from<User>()
    .include(&User::profile)
    .include(&User::posts);

std::vector<User> users = co_await db.fetch_all(query);

for (const auto& user : users) {
    std::cout << user.username << ":\n";
    if (user.profile.has_value()) {
        std::cout << "  Website: " << user.profile->website << "\n";
    }
    for (const auto& post : user.posts) {
        std::cout << "  Post: " << post.title << "\n";
    }
}
```

### Scoped Eager Loading (Nested Filtering & Sorting)

Filter and order child relations directly during eager loading:

```cpp
auto query = db.from<User>()
    .include(&User::posts, [](SelectBuilder<Post>& post_query) {
        post_query
            .where(&Post::title, Op::Like, "C++26%")
            .order_by(&Post::id, SortOrder::Desc)
            .limit(5);
    });

std::vector<User> users = co_await db.fetch_all(query);
```

---

## Relational Filtering (`where_has` / `where_doesnt_have`)

Filter parent records based on the existence or absence of matching child records without manual SQL joins:

```cpp
// Users who have at least one post matching "Release"
auto q1 = db.from<User>()
    .where_has(&User::posts, [](auto& pq) {
        pq.where(&Post::title, Op::Like, "%Release%");
    });

// Users who do NOT have any posts
auto q2 = db.from<User>()
    .where_doesnt_have(&User::posts);
```

---

## Lazy Loading (`.load()`)

When an entity is loaded without `.include()`, its relations remain unpopulated (`user.posts.is_loaded() == false`). You can load relations on-demand using `co_await user.posts.load(db)`:

```cpp
auto user = co_await db.find_by_id<User>(1);

if (user && !user->posts.is_loaded()) {
    // Lazily fetches posts for this user
    co_await user->posts.load(db);

    for (const auto& post : user->posts) {
        std::cout << post.title << "\n";
    }
}
```

---

## Many-to-Many Relationships

Many-to-many associations utilize a junction table:

```cpp
struct UserRole {
    uint64_t user_id{0};
    uint64_t role_id{0};

    static auto schema() {
        return TableDef<UserRole>("user_roles")
            .column(&UserRole::user_id).references(&User::id).on_delete_cascade()
            .column(&UserRole::role_id).references(&Role::id).on_delete_cascade();
    }
};
```

Register the relation on `User`:

```cpp
table.many_to_many(&User::roles, &UserRole::user_id, &UserRole::role_id);
```

### Linking and Unlinking Records

Use `db.link` and `db.unlink` to insert or remove junction records:

```cpp
// Add role 3 to user 10
co_await db.link<UserRole>(10, 3);

// Remove role 3 from user 10
co_await db.unlink<UserRole>(10, 3);
```
