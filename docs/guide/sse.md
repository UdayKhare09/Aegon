# Server-Sent Events (SSE)

Aegon provides native, high-throughput support for **Server-Sent Events (RFC 8895)** integrated directly into its C++26 coroutine architecture and `io_uring` event loop.

A single unified API (`co_await ctx.sse()`) streams real-time events seamlessly across **HTTP/1.1**, **HTTP/2**, and **HTTP/3 (QUIC)** with zero boilerplate, zero manual wire formatting, and compile-time JSON serialization.

---

## Why Server-Sent Events?

Server-Sent Events (SSE) provide an efficient, unidirectional HTTP streaming channel from server to client over standard HTTP:

* **Simpler than WebSockets**: Runs over standard HTTP without custom handshake protocols, proxies, or firewall workarounds.
* **Built-in Browser Support**: Automatically handled by browser `EventSource` with auto-reconnect, event IDs, and event filtering.
* **Ideal for Real-Time Feeds**: Perfect for LLM token streaming, live dashboards, financial market tickers, log feeds, and telemetry.
* **Transparent Multi-Protocol**: Operates using chunked transfer encoding in HTTP/1.1, multiplexed DATA frames in HTTP/2, and QUIC stream frames in HTTP/3.

---

## Quick Start

Upgrading a request to an SSE stream requires only calling `co_await ctx.sse()`, which returns an [`SseStream`](#ssestream-api-reference):

```cpp
#include "http/Server.h"
#include "http/SseStream.h"

using namespace aegon::http;

router.get("/events", [](Context& ctx) -> aegon::core::Task<void> {
    // 1. Upgrade response to text/event-stream
    auto stream = co_await ctx.sse();

    // 2. Stream events asynchronously
    co_await stream.event("greeting", "Hello from Aegon!");
    co_await stream.comment("heartbeat");
    co_await stream.retry(3000);
    co_await stream.data("Plain data message");

    // 3. Close stream when finished (or simply return from handler)
    stream.close();
});
```

> [!TIP]
> Calling `co_await ctx.sse()` automatically sets `Content-Type: text/event-stream`, `Cache-Control: no-cache`, and `X-Accel-Buffering: no` on the response.

---

## `SseStream` API Reference

When you call `co_await ctx.sse()`, you receive an `SseStream` object that exposes the full RFC 8895 event lifecycle.

### 1. Named Event (`.event()`)
Emits a typed event block with an `event:` name and `data:` payload:

```cpp
co_await stream.event("trade", "BUY 100 AAPL @ $185.20");
```

**Wire Output:**
```http
event: trade
data: BUY 100 AAPL @ $185.20

```

---

### 2. Anonymous Data (`.data()`)
Emits a standard `data:` block without an explicit event name:

```cpp
co_await stream.data("System load normal");
```

**Wire Output:**
```http
data: System load normal

```

---

### 3. Compile-Time JSON Events (`.send_json<T>()`)
Serializes any C++ struct or container directly into the `data` field using Aegon's zero-copy Glaze engine:

```cpp
struct OrderStatus {
    std::string order_id;
    std::string state;
    double amount;
};

OrderStatus status{.order_id = "ord-9821", .state = "FILLED", .amount = 1450.50};

co_await stream.send_json("order_update", status);
```

**Wire Output:**
```http
event: order_update
data: {"order_id":"ord-9821","state":"FILLED","amount":1450.5}

```

---

### 4. Heartbeat Comments (`.comment()`)
Emits a comment line prefixed by `: `. Comments are ignored by browser client parsers and are used to keep connections alive through intermediary NATs, proxies, and load balancers:

```cpp
co_await stream.comment("ping");
```

**Wire Output:**
```http
: ping

```

---

### 5. Client Reconnect Interval (`.retry()`)
Instructs the client `EventSource` how many milliseconds to wait before attempting to reconnect if the connection drops:

```cpp
// Supports integer milliseconds or std::chrono::milliseconds
co_await stream.retry(5000);                          // 5 seconds
co_await stream.retry(std::chrono::milliseconds(2500)); // 2.5 seconds
```

**Wire Output:**
```http
retry: 5000

```

---

### 6. Closing the Stream (`.close()`)
Marks the stream as complete. Any subsequent write calls will safely be no-ops:

```cpp
stream.close();
assert(!stream.is_open());
```

> [!NOTE]
> Explicitly calling `stream.close()` is optional. When your coroutine handler returns, Aegon automatically flushes any remaining buffered frames and closes the stream with the appropriate protocol terminator.

---

## Protocol Engine Behavior

Aegon transparently handles the protocol differences between HTTP versions:

| Protocol | Framing Mechanism | Content-Length | Termination |
| :--- | :--- | :--- | :--- |
| **HTTP/1.1** | `Transfer-Encoding: chunked` | Suppressed | Zero-chunk (`0\r\n\r\n`) + connection close |
| **HTTP/2** | Native `DATA` frames | None | `END_STREAM` flag on final frame |
| **HTTP/3** | Native QUIC Stream frames | None | `FIN` bit set on QUIC stream |

### HTTP/1.1 Header Ordering Guarantees
In HTTP/1.1 chunked mode, Aegon guarantees that the `HTTP/1.1 200 OK` status line and response headers are flushed to the socket before the first chunk is sent.

### HTTP/2 & HTTP/3 Flow Control
In HTTP/2 and HTTP/3, each call to `event()`, `data()`, or `send_json()` immediately queues the serialized frame, resumes the protocol state machine (`nghttp2_session_resume_data` / `nghttp3_conn_resume_stream`), and flushes the packet to the wire without waiting for the handler to complete.

---

## Client Integration

### 1. Browser JavaScript (`EventSource`)

```html
<!DOCTYPE html>
<html>
<body>
  <div id="feed"></div>

  <script>
    const es = new EventSource('/events');

    // Default message handler (for stream.data(...))
    es.onmessage = (event) => {
      console.log('Default data:', event.data);
    };

    // Named event listener (for stream.event("tick", ...))
    es.addEventListener('tick', (event) => {
      const tick = JSON.parse(event.data);
      console.log('Tick:', tick.symbol, tick.price);
    });

    es.onerror = (err) => {
      console.error('SSE Error:', err);
    };
  </script>
</body>
</html>
```

---

### 2. Command Line (`curl`)

Inspect live streams across all three protocol versions:

#### HTTP/1.1
```bash
curl -N --http1.1 http://127.0.0.1:8080/events
```

#### HTTP/2 Cleartext (`h2c`)
```bash
curl -N --http2-prior-knowledge http://127.0.0.1:8080/events
```

#### HTTP/3 over QUIC
```bash
curl -k -N --http3-only https://127.0.0.1:8443/events
```

---

## LLM / AI Token Streaming Example

A common use case for SSE is streaming generative AI or LLM output token-by-token:

```cpp
router.post("/v1/chat/completions", [](Context& ctx) -> aegon::core::Task<void> {
    auto prompt = ctx.req().body();
    auto stream = co_await ctx.sse();

    // Stream generated tokens as they arrive
    std::vector<std::string> tokens = {"The", " future", " of", " C++", " is", " coroutines."};

    for (const auto& token : tokens) {
        co_await stream.event("token", token);
        // Small delay simulation
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    co_await stream.event("done", "[DONE]");
    stream.close();
});
```

---

## Live Sample Application

A full runnable demonstration application is included in the Aegon repository under [`samples/sse`](file:///home/uday/Projects/Aegon/samples/sse):

```bash
# Build the sample app
cmake --build build --target aegon_sse_sample -j$(nproc)

# Run the live multi-protocol test suite
./samples/sse/test_live.sh
```
