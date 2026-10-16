# I/O Engine Subsystem (`IoEngine`)

Aegon is built upon a high-performance, modular I/O engine architecture centered around the abstract [`aegon::core::IoEngine`](file:///home/uday/Projects/Aegon/src/core/engine/IoEngine.h) interface. 

To maximize throughput and adoption, Aegon provides pluggable I/O backends:
1. **`IoUringEngine` (Default)**: Linux 6.1+ asynchronous kernel engine leveraging `io_uring` multishot primitives, provided buffer rings (`PBUF_RING`), single-issuer cooperative taskrun, and zero-copy transfers.
2. **`EpollEngine`**: High-performance fallback engine for Linux kernels < 6.1, WSL2, and restricted container/Docker environments where unprivileged `io_uring` syscalls are restricted or unavailable.

The engine abstraction uses **zero-allocation stack awaiters**, ensuring that polymorphic dispatch incurs **0 measurable runtime overhead** compared to direct kernel calls.

---

## Architecture Overview

```
                               ┌────────────────────────────────┐
                               │       aegon::core::IoEngine    │
                               │        (Pure Interface)        │
                               └───────────────▲────────────────┘
                                               │
                      ┌────────────────────────┴────────────────────────┐
                      │                                                 │
       ┌──────────────┴───────────────┐                  ┌──────────────┴──────────────┐
       │   core::IoUringEngine        │                  │     core::EpollEngine       │
       │   (Linux Kernel 6.1+)        │                  │   (Kernels < 6.1, WSL2,     │
       │                              │                  │    Docker Containers)       │
       ├──────────────────────────────┤                  ├─────────────────────────────┤
       │ • Kernel PBUF_RING           │                  │ • User-Space Freelist Pool  │
       │ • Multishot Accept & Recv    │                  │ • epoll_create1 & wait      │
       │ • IORING_SETUP_DEFER_TASKRUN │                  │ • Monotonic Timer Min-Heap  │
       │ • Zero-Copy Send (SEND_ZC)   │                  │ • Splice(2) Zero-Copy Files │
       │ • eventfd Cross-Thread Wake  │                  │ • eventfd Cross-Thread Wake │
       └──────────────────────────────┘                  └─────────────────────────────┘
```

---

## Backend Selection Precedence & Fail-Fast Guarantees

Aegon uses a strict 3-tier precedence hierarchy to determine the engine backend:

$$\textbf{Programmatic Explicit Config} \;\;>\;\; \textbf{Environment Variable (AEGON\_ENGINE)} \;\;>\;\; \textbf{Auto-Probing}$$

### 1. Programmatic Selection (`config.type`)
Takes the highest priority. If explicitly set, environment variables are completely ignored:
- `EngineType::Epoll`: Forces `EpollEngine`.
- `EngineType::IoUring`: Explicitly selects `IoUringEngine`. If initialization fails (e.g. unsupported kernel or seccomp block), it **fails fast and throws `std::system_error`** without fallback, preventing unexpected degradation in production.

```cpp
#include <aegon/core/engine/IoEngine.h>
#include <aegon/core/EventLoop.h>

using namespace aegon::core;

EngineConfig config{
    .type = EngineType::IoUring, // Explicitly require io_uring (fail-fast on failure)
    .entries = 4096
};

EventLoop loop(config, /*buffer_pool_entries=*/8192, /*buffer_size=*/4096);
```

### 2. Environment Variable Override (`AEGON_ENGINE`)
Applied when `config.type` is `EngineType::Auto` (the default):
- `AEGON_ENGINE=epoll`: Forces `EpollEngine`.
- `AEGON_ENGINE=io_uring`: Explicitly requests `IoUringEngine`. Similar to programmatic selection, this acts as an explicit requirement: if `io_uring` cannot initialize, Aegon **fails fast and throws without fallback**.

```bash
# Force epoll backend
AEGON_ENGINE=epoll ./my_service

# Require io_uring backend (fails fast if kernel rejects io_uring)
AEGON_ENGINE=io_uring ./my_service
```

### 3. Automatic Kernel Probing & Graceful Fallback
When `config.type == EngineType::Auto` and no environment variable is set:
- Aegon attempts to initialize `IoUringEngine` with high-performance flags (`IORING_SETUP_DEFER_TASKRUN`, `IORING_SETUP_SINGLE_ISSUER`).
- If initialization fails (e.g. Linux kernels < 6.1, WSL2 without `io_uring`, or Docker container restrictions), Aegon logs a warning and **gracefully falls back to `EpollEngine`** with zero code changes.

---

## Asynchronous Awaiter Builders

Every asynchronous operation is submitted through `loop.engine()`. All methods return an `IoAwaiter` derivative that can be directly `co_await`ed. 

Awaiters are allocated directly within the calling coroutine's stack frame, eliminating dynamic heap allocations on the hot path.

### Socket & Network Operations

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `engine.accept(listen_fd)` | `AcceptAwaiter` | Accepts a single incoming connection. |
| `engine.accept_multishot(listen_fd)` | `MultishotAcceptStream` | Continually armed multishot accept stream. |
| `engine.recv_provided(fd, bgid)` | `RecvProvidedAwaiter` | Receives data directly into a pool-provided buffer. |
| `engine.recv_multishot(fd, bgid)` | `MultishotRecvStream` | Streaming multishot receive using buffer pool. |
| `engine.recv(fd, buf, len, flags)` | `RecvAwaiter` | Receives into an explicit caller-allocated buffer. |
| `engine.send(fd, data)` | `SendAwaiter` | Transmits a byte buffer (`std::string_view` or `std::span`). |
| `engine.send_all(fd, data)` | `Task<int>` | Resilient coroutine loop ensuring all bytes are transmitted. |
| `engine.send_zc(fd, data)` | `SendZcAwaiter` | Zero-copy transmit (`io_uring_prep_send_zc` on io_uring). |
| `engine.connect(fd, addr, len)` | `ConnectAwaiter` | Asynchronous non-blocking TCP connect. |
| `engine.poll(fd, mask)` | `PollAwaiter` | Waits for descriptor readiness (POLLIN, POLLOUT, etc.). |
| `engine.recvmsg(fd, msghdr*)` | `RecvmsgAwaiter` | Asynchronous datagram reception for UDP & HTTP/3. |

### File & System Operations

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `engine.splice(in, o_in, out, o_out, len)` | `SpliceAwaiter` | Zero-copy pipe/file-to-socket transfer (`splice(2)`). |
| `engine.timeout(nanoseconds)` | `TimeoutAwaiter` | High-precision asynchronous sleep / timer. |
| `engine.close(fd)` | `CloseAwaiter` | Asynchronous file/socket descriptor closure. |
| `engine.shutdown(fd, how)` | `ShutdownAwaiter` | Asynchronous socket shutdown (`SHUT_RD`, `SHUT_WR`). |
| `engine.cancel(user_data, flags)` | `CancelAwaiter` | Cancels an in-flight asynchronous request. |

---

## Dual-Mode BufferPool Integration

`BufferPool` seamlessly adapts to whichever engine backend is active:
- **Under `IoUringEngine`**: Buffers are registered in the kernel's `PBUF_RING` (`io_uring_buf_ring`). When incoming network packets arrive, the Linux kernel assigns a buffer ID automatically and returns it in the CQE flags.
- **Under `EpollEngine`**: Buffers are managed in user space using a lock-free thread-local freelist. When socket readiness is signaled by `epoll_wait`, `EpollEngine` allocates a buffer from the freelist, executes non-blocking `recv(2)`, and flags the result with the buffer ID.

At the application level (`Http1Connection`, `Http2Connection`, `TlsStream`), the buffer consumption and recycling API is identical:

```cpp
auto recv_res = co_await loop.engine().recv_provided(client_fd, loop.buffer_pool().bgid());
if (recv_res.bytes > 0) {
    auto slice = loop.buffer_pool().get_buffer(recv_res.bid, recv_res.bytes);
    // Parse protocol headers in-place from slice...
    
    // Recycle buffer back to pool
    loop.buffer_pool().return_buffer(recv_res.bid);
}
```

---

## Performance Comparison (2 CPU Cores)

The engine abstraction was benchmarked on isolated hardware cores to verify that the virtual engine interface introduced zero regression:

| Protocol Suite | `IoUringEngine` Throughput | `EpollEngine` Throughput | `Epoll` Efficiency vs `io_uring` |
| :--- | :--- | :--- | :--- |
| **HTTP/1.1 Plaintext** | **528,851 req/s** | **454,290 req/s** | 86.0% |
| **HTTP/1.1 TLS 1.3** | **398,003 req/s** | **350,448 req/s** | 88.0% |
| **HTTP/2 Multiplexing** | **1,350,621 req/s** | **1,316,487 req/s** | 97.5% |
| **HTTP/3 (QUIC)** | **836,720 req/s** | **873,842 req/s** | 104.4% (Direct userspace UDP recv) |

---

## Platform Roadmap

The `IoEngine` interface lays the foundation for cross-platform adoption across operating systems:
- [x] **Linux 6.1+**: `IoUringEngine` (native io_uring)
- [x] **Linux < 6.1, WSL2, Docker**: `EpollEngine` (epoll)
- [ ] **macOS / BSD**: `KqueueEngine` (kqueue & kevent)
- [ ] **Windows**: `IocpEngine` (I/O Completion Ports)
