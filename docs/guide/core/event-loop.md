# Event Loop Architecture

`aegon::core::EventLoop` is the heartbeat of an Aegon worker thread. It unifies a dedicated `IoUring` instance with a `BufferPool` to process asynchronous events with zero lock contention.

---

## Thread-per-Core Shared-Nothing Design

Aegon follows the modern **thread-per-core** architecture:

```
[ Worker Thread 0 ] ──── Pin to Core 0 ────► EventLoop 0 (Ring 0 + BufferPool 0)
[ Worker Thread 1 ] ──── Pin to Core 1 ────► EventLoop 1 (Ring 1 + BufferPool 1)
[ Worker Thread 2 ] ──── Pin to Core 2 ────► EventLoop 2 (Ring 2 + BufferPool 2)
[ Worker Thread 3 ] ──── Pin to Core 3 ────► EventLoop 3 (Ring 3 + BufferPool 3)
```

- Each worker thread runs its own isolated `EventLoop`.
- Threads do not share memory rings, mutexes, or run-queues.
- Hardware interrupts and network sockets are distributed evenly across cores by Linux `SO_REUSEPORT`.

---

## Public Methods

```cpp
#include <aegon/core/EventLoop.h>

using aegon::core::EventLoop;
```

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `run()` | `void` | Starts the event loop. Blocks until `stop()` is called. |
| `stop()` | `void` | Requests the loop to exit cleanly on its next iteration. |
| `spawn(Task<void>)` | `void` | Enqueues and starts a root asynchronous task on the loop. |
| `ring()` | `IoUring&` | Direct reference to this loop's `IoUring` engine. |
| `buffer_pool()` | `BufferPool&` | Direct reference to this loop's `BufferPool` instance. |
| `is_running()` | `bool` | Returns `true` if the event loop is actively running. |

### Static Utilities

| Static Method | Return Type | Description |
| :--- | :--- | :--- |
| `EventLoop::current()` | `EventLoop*` | Returns pointer to the current thread's active `EventLoop` (or `nullptr`). |
| `EventLoop::pin_to_core(int core_id)` | `void` | Uses `pthread_setaffinity_np` to pin the calling thread to a specific CPU core. |

---

## Standalone Usage Example

While `Server` manages event loops automatically, you can run standalone event loops for custom microservices or background engines:

```cpp
int main() {
    // 1. Create event loop with 4096 ring entries
    EventLoop loop(4096);

    // 2. Pin to physical CPU core 2
    EventLoop::pin_to_core(2);

    // 3. Spawn asynchronous background task
    loop.spawn([](EventLoop& l) -> Task<void> {
        std::cout << "Running on thread-pinned core!\n";
        co_await l.ring().timeout(1'000'000'000ULL); // 1 second timer
        std::cout << "Timer expired!\n";
        l.stop();
        co_return;
    }(loop));

    // 4. Run loop
    loop.run();

    return 0;
}
```
