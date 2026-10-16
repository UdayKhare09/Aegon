# Provided Buffer Ring (BufferPool)

In traditional network servers handling 100,000+ idle persistent connections, allocating a 64KB read buffer for each open socket consumes gigabytes of resident memory (100,000 × 64KB = 6.4GB), even when no traffic is flowing.

Aegon eliminates this memory waste using modern Linux **Provided Buffer Rings (`PBUF_RING`)** via `aegon::core::BufferPool`.

---

## How It Works

Instead of binding memory to specific file descriptors, user space registers a shared ring of fixed-size memory buffers with the kernel under a Buffer Group ID (`bgid`):

```
              [ User-Space BufferPool ]
           BGID = 1, Entries = 2048 (4KB each)
                         │
                         ▼ (Registered with kernel)
              [ Linux Kernel PBUF_RING ]
                         │
      When network packets arrive on any socket:
      1. Kernel picks an available buffer ID (bid)
      2. DMA transfers packet payload into buffer
      3. CQE returns bid and byte count to user space
                         │
                         ▼
        ctx.req() parses buffer slice in-place
                         │
                         ▼
             pool.return_buffer(bid)
       (Recycled back to kernel without malloc/free)
```

Sockets do not hold dedicated receive buffers while idle. Memory is consumed only while actively processing an inbound request.

---

## Dual-Mode Architecture

`BufferPool` operates seamlessly in two distinct modes depending on which `IoEngine` is active:

1. **Linux Kernel `io_uring` Mode (`PBUF_RING`)**:
   - Registered with the kernel via `io_uring_register_buf_ring`.
   - The Linux kernel automatically selects an available buffer slot upon packet ingress.
   - Buffer recycling executes via `io_uring_buf_ring_advance`.

2. **User-Space Freelist Mode (`EpollEngine`)**:
   - Operates entirely in user space without requiring kernel ring support.
   - An internal zero-allocation freelist provides instant $O(1)$ buffer allocations.
   - When socket readiness is signaled, `EpollEngine` reserves a buffer slot, receives into it, and returns the buffer identifier.

---

## Public API Reference

```cpp
#include <aegon/core/BufferPool.h>

using aegon::core::BufferPool;
```

### Constructor

```cpp
BufferPool(uint16_t bgid = 1, 
           uint16_t entries = 2048, 
           size_t buffer_size = 4096, 
           bool register_buffers = true);
```

- `bgid`: Buffer Group Identifier referenced in `recv_provided(fd, bgid)`.
- `entries`: Number of buffer slots (must be a power of 2).
- `buffer_size`: Size of each buffer slot (default 4KB).
- `register_buffers`: Pre-registers memory with the kernel ring for zero-copy fixed buffer transmission under `io_uring`.

---

## Receiving and Recycling Buffers

When an asynchronous receive operation completes:

```cpp
RecvResult res = co_await loop.engine().recv_provided(client_fd, pool.bgid());

if (res.bytes > 0) {
    // 1. Obtain zero-copy span slice into the populated buffer
    std::span<uint8_t> data = pool.get_buffer(res.bid, res.bytes);

    // 2. Parse request payload in-place...
    // ...

    // 3. Recycle the buffer slot back to the pool
    pool.return_buffer(res.bid);
}
```

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `get_buffer(bid, bytes)` | `std::span<uint8_t>` | Returns memory slice for the buffer ID assigned by the engine. |
| `return_buffer(bid)` | `void` | Returns buffer slot back to the active engine (deferred kernel ring advance or freelist push). |
| `alloc_buffer()` | `int` | Allocates a buffer slot from the freelist (used by `EpollEngine`). |
| `bgid()` | `uint16_t` | Returns the assigned buffer group identifier. |
| `buffer_size()` | `size_t` | Size in bytes of each individual buffer slot. |
| `entries()` | `uint16_t` | Total number of buffer slots. |
| `flush_returns()` | `void` | Flushes deferred buffer advances to the kernel ring at the end of the event loop iteration. |
| `raw_memory()` | `uint8_t*` | Pointer to the base of the contiguous memory allocation. |
