# io_uring Kernel Subsystem

The `aegon::core::IoUring` class provides an object-oriented, coroutine-native C++26 interface directly over the Linux kernel's `io_uring` asynchronous I/O subsystem.

It leverages state-of-the-art Linux 6.x features: **Multishot Accept**, **Multishot Recv with Provided Buffer Rings (`PBUF_RING`)**, **Zero-Copy Send (`IORING_OP_SEND_ZC`)**, **Zero-Copy File Splice**, and **Kernel Submission Polling (`SQPOLL`)**.

---

## Configuration (`IoUringConfig`)

```cpp
#include <aegon/core/IoUring.h>

using namespace aegon::core;

IoUringConfig config{
    .entries = 8192,                // SQ ring size (power of 2)
    .flags = 0,                     // Additional IORING_SETUP_* flags
    .enable_sqpoll = true,          // Dedicated kernel SQPOLL worker thread
    .sq_thread_idle_ms = 2000,      // Thread sleep threshold in milliseconds
    .sq_thread_cpu = 3              // Pin kernel polling thread to CPU core 3
};

IoUring ring(config);
```

---

## Asynchronous Awaiter Builders

Every operation on `IoUring` returns an awaiter struct that can be directly `co_await`ed. Submission occurs at the suspension point (`await_suspend`), and resumption occurs upon CQE delivery.

### Networking & Socket Operations

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `ring.accept(listen_fd)` | `MultishotAcceptAwaiter` | Accepts a single connection asynchronously. |
| `ring.accept_multishot(listen_fd)` | `MultishotAcceptStream` | Continually armed multishot accept stream. |
| `ring.recv_multishot(fd, bgid)` | `MultishotRecvAwaiter` | Kernel auto-assigns incoming data to buffer ring `bgid`. |
| `ring.recv(fd, buf, len, flags)` | `RecvAwaiter` | Standard async socket receive into user buffer. |
| `ring.connect(fd, addr, addr_len)` | `ConnectAwaiter` | Asynchronous non-blocking socket connect. |
| `ring.send(fd, data)` | `SendAwaiter` | Async socket transmission (`std::string_view` or `span`). |
| `ring.send_all(fd, data)` | `Task<int>` | Resilient transmission loop guaranteeing all bytes are sent. |
| `ring.send_zc(fd, data)` | `SendZcAwaiter` | Zero-copy transmission (`IORING_OP_SEND_ZC`). |
| `ring.send_zc_fixed(fd, ...)` | `SendZcAwaiter` | Zero-copy transmission using pre-registered fixed buffers. |
| `ring.recvmsg(fd, msghdr*)` | `RecvmsgAwaiter` | Asynchronous datagram reception (UDP / HTTP/3). |

### File & System Operations

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `ring.splice(in, off_in, out, ...)`| `SpliceAwaiter` | Zero-copy kernel splice between file and socket. |
| `ring.close(fd)` | `CloseAwaiter` | Asynchronous descriptor closure. |
| `ring.cancel(user_data, flags)` | `CancelAwaiter` | Cancels a currently armed in-flight request. |
| `ring.timeout(nanoseconds)` | `TimeoutAwaiter` | Coroutine sleep / timer using `IORING_OP_TIMEOUT`. |

---

## Multishot Accept Stream (`MultishotAcceptStream`)

Traditional server architectures submit an accept syscall for every single connection. Aegon uses Linux `IORING_ACCEPT_MULTISHOT`: a single request is submitted once and remains armed in the kernel, generating completion events for incoming connections without repeated SQE allocations:

```cpp
MultishotAcceptStream stream = ring.accept_multishot(listen_socket);

while (loop.is_running()) {
    AcceptResult res = co_await stream.next();
    if (res.fd < 0) break;

    // Dispatch accepted socket descriptor
    loop.spawn(handle_connection(loop, res.fd));
}
```

---

## Ring Processing Loop

Inside `EventLoop`, completion queue entries (CQEs) are processed in batches:

```cpp
// Submit in-flight SQEs and sleep until at least 1 completion arrives
ring.submit_and_wait(/*min_complete=*/1);

// Harvest CQEs and resume awaiting coroutines
size_t completed = ring.process_completions();
```

`process_completions()` inspects `cqe->user_data`, casts it back to `IoAwaiter*`, and invokes `on_completion(res, flags)`, immediately resuming the coroutine without dynamic dispatch overhead.
