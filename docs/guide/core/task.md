# Task & Coroutines

`aegon::core::Task<T>` is Aegon's fundamental asynchronous primitive. It implements a lazy C++26 coroutine powered by **symmetric transfer** (`std::coroutine_handle<>::await_suspend`).

---

## Why Symmetric Transfer?

In traditional coroutine implementations, resuming an awaiting coroutine requires regular function call recursion. In high-throughput network engines processing hundreds of thousands of chained I/O events, deep coroutine chains cause stack overflows.

With symmetric transfer, control is handed directly from the completed coroutine to the continuation coroutine via CPU register exchange:

```
[ Coroutine A ] ──── co_await ────► [ Coroutine B ]
       ▲                                   │
       │           (Symmetric Transfer)    │
       └────────────── Resume ─────────────┘
       (No stack frame growth, zero overhead)
```

---

## Public Methods & Semantics

`Task<T>` is a move-only, `[[nodiscard]]` lazy type:

```cpp
#include <aegon/core/Task.h>

using aegon::core::Task;
```

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `is_ready()` | `bool` | Returns `true` if the coroutine is null or has finished execution (`done()`). |
| `resume()` | `void` | Manually resumes the coroutine (used by top-level event loops). |
| `result()` | `T` / `T&` | Returns the return value; rethrows any exception captured during execution. |
| `operator co_await() &&` | `Awaiter` | Symmetric transfer awaiter. Requires an rvalue (`co_await std::move(task)` or `co_await fn()`). |

---

## Writing Coroutines

Coroutines return `Task<T>` (or `Task<void>`) and use `co_await` / `co_return`:

```cpp
Task<std::string> fetch_user_token(uint64_t user_id) {
    // Asynchronous database query
    auto user = co_await db.find_by_id<User>(user_id);
    if (!user) {
        co_return "";
    }
    co_return user->token;
}

Task<void> handle_request(Context& ctx) {
    std::string token = co_await fetch_user_token(123);
    ctx.res().text("Token: " + token);
    co_return;
}
```

---

## Exception Propagation

Unhandled exceptions thrown inside a `Task<T>` are caught by its promise object (`unhandled_exception()`). 

When another coroutine `co_await`s that task, the exception is re-thrown at the suspension point:

```cpp
Task<void> risky_work() {
    throw std::runtime_error("Disk I/O failed");
    co_return;
}

Task<void> caller() {
    try {
        co_await risky_work();
    } catch (const std::exception& e) {
        std::cerr << "Caught error: " << e.what() << "\n";
    }
    co_return;
}
```
