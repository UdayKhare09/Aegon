# io_uring Kernel Subsystem (`IoUringEngine`)

The [`aegon::core::IoUringEngine`](file:///home/uday/Projects/Aegon/src/core/engine/IoUringEngine.h) backend implements the abstract [`IoEngine`](./io-engine.md) interface directly over the Linux kernel's `io_uring` asynchronous I/O subsystem.

It leverages state-of-the-art Linux 6.x features: **Multishot Accept**, **Multishot Recv with Provided Buffer Rings (`PBUF_RING`)**, **Zero-Copy Send (`IORING_OP_SEND_ZC`)**, **Zero-Copy File Splice**, and **Single-Issuer Cooperative Taskrun (`IORING_SETUP_DEFER_TASKRUN`)**.

> [!NOTE]
> For general engine usage, awaiters reference, and comparative benchmarks across `io_uring` and `epoll`, see the comprehensive [I/O Engine & Backends Guide](./io-engine.md).

---

## Configuration (`EngineConfig`)

```cpp
#include <aegon/core/engine/IoUringEngine.h>

using namespace aegon::core;

EngineConfig config{
    .type = EngineType::IoUring,
    .entries = 8192,                // SQ ring size (power of 2)
    .flags = 0                      // Additional IORING_SETUP_* flags
};

IoUringEngine engine(config);
```

---

## Asynchronous Awaiter Builders

Every operation on `IoUring` returns an awaiter struct that can be directly `co_await`ed. Submission occurs at the suspension point (`await_suspend`), and resumption occurs upon CQE delivery.

### Networking & Socket Operations

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `engine.accept(listen_fd)` | `AcceptAwaiter` | Accepts a single connection asynchronously. |
| `engine.accept_multishot(listen_fd)` | `MultishotAcceptStream` | Continually armed multishot accept stream. |
| `engine.recv_provided(fd, bgid)` | `RecvProvidedAwaiter` | Kernel auto-assigns incoming data to buffer ring `bgid`. |
| `engine.recv_multishot(fd, bgid)` | `MultishotRecvStream` | Kernel streaming multishot receive into buffer ring `bgid`. |
| `engine.recv(fd, buf, len, flags)` | `RecvAwaiter` | Standard async socket receive into user buffer. |
| `engine.connect(fd, addr, addr_len)` | `ConnectAwaiter` | Asynchronous non-blocking socket connect. |
| `engine.send(fd, data)` | `SendAwaiter` | Async socket transmission (`std::string_view` or `span`). |
| `engine.send_all(fd, data)` | `Task<int>` | Resilient transmission loop guaranteeing all bytes are sent. |
| `engine.send_zc(fd, data)` | `SendZcAwaiter` | Zero-copy transmission (`IORING_OP_SEND_ZC`). |
| `engine.recvmsg(fd, msghdr*)` | `RecvmsgAwaiter` | Asynchronous datagram reception (UDP / HTTP/3). |

### File & System Operations

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `engine.splice(in, off_in, out, ...)`| `SpliceAwaiter` | Zero-copy kernel splice between file and socket. |
| `engine.close(fd)` | `CloseAwaiter` | Asynchronous descriptor closure. |
| `engine.shutdown(fd, how)` | `ShutdownAwaiter` | Asynchronous socket shutdown (`SHUT_RD`, `SHUT_WR`). |
| `engine.cancel(user_data, flags)` | `CancelAwaiter` | Cancels a currently armed in-flight request. |
| `engine.timeout(nanoseconds)` | `TimeoutAwaiter` | Coroutine sleep / timer using `IORING_OP_TIMEOUT`. |

---

## Multishot Accept Stream (`MultishotAcceptStream`)

Traditional server architectures submit an accept syscall for every single connection. Aegon uses Linux `IORING_ACCEPT_MULTISHOT`: a single request is submitted once and remains armed in the kernel, generating completion events for incoming connections without repeated SQE allocations:

```cpp
MultishotAcceptStream stream = engine.accept_multishot(listen_socket);

while (loop.is_running()) {
    AcceptResult res = co_await stream.next();
    if (res.fd < 0) break;

    // Dispatch accepted socket descriptor
    loop.spawn(handle_connection(loop, res.fd));
}
```

---

## Engine Processing Loop

Inside `EventLoop`, completion queue entries (CQEs) are processed in batches:

```cpp
// Submit in-flight SQEs and sleep until at least 1 completion arrives
engine.submit_and_wait(/*min_complete=*/1);

// Harvest CQEs and resume awaiting coroutines
size_t completed = engine.process_completions();
```

`process_completions()` inspects `cqe->user_data`, casts it back to `IoAwaiter*`, and invokes `on_completion(res, flags)`, immediately resuming the coroutine without dynamic dispatch overhead.
