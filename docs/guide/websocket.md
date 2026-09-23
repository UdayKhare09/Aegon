# WebSocket (RFC 6455)

Aegon provides a high-performance, RFC 6455 compliant WebSocket implementation directly integrated into its `io_uring` event loop and C++26 coroutine architecture.

It features **5-tier SIMD & SWAR hardware unmasking**, **zero-copy frame parsing**, **in-place payload transformation**, **automatic backpressure management via `send_all`**, and **high-throughput frame batching**.

---

## Route Registration Patterns

Aegon offers multiple route registration signatures depending on your application needs:

### 1. Default Fast-Path Echo Endpoint
When no handler is passed, Aegon automatically registers an ultra-low latency, zero-allocation echo handler that batches frames directly into contiguous outbound buffers:

```cpp
// Both forms are identical:
server.ws("/ws");
server.ws_echo("/echo");
```

### 2. Message-Level Echo Callback (`WebSocketEchoHandler`)
Use this when you want to intercept every incoming message and run lightweight asynchronous transformations before echoing:

```cpp
server.ws("/transform", [](WebSocket& ws, Message msg) -> aegon::core::Task<void> {
    if (msg.is_text()) {
        std::string reply = "Received: " + std::string(msg.text());
        co_await ws.send_text(reply);
    } else if (msg.is_binary()) {
        co_await ws.send_binary(msg.binary());
    }
});
```

### 3. Connection-Level Lifecycle Handler (`WebSocketHandler`)
For interactive applications (chat rooms, notifications, telemetry, gaming), register a connection handler where you bind individual event callbacks:

```cpp
server.ws("/chat", [](WebSocket& ws) -> aegon::core::Task<void> {
    // 1. Connection established
    std::cout << "Client joined " << ws.path() << " on fd " << ws.fd() << "\n";

    // 2. Register callbacks on the 'ws' connection instance
    ws.on_text([](WebSocket& ws, std::string_view text) -> aegon::core::Task<void> {
        co_await ws.send_text("Echo: " + std::string(text));
    });

    ws.on_binary([](WebSocket& ws, std::span<const uint8_t> data) -> aegon::core::Task<void> {
        co_await ws.send_binary(data);
    });

    ws.on_close([](WebSocket& ws, CloseCode code, std::string_view reason) -> aegon::core::Task<void> {
        std::cout << "Client left. Code: " << static_cast<uint16_t>(code) << "\n";
        co_return;
    });

    co_return;
});
```

### 4. Router-Level Binding
You can register WebSocket routes on a `Router` instance before attaching it to the `Server`:

```cpp
Router router;
router.ws("/feed", feed_handler);
router.ws_echo("/feed/echo");

Server server(std::move(router));
```

> [!NOTE]
> When a WebSocket route is registered, Aegon automatically registers a fallback HTTP `GET` handler for that path that returns `426 Upgrade Required` with `Upgrade: websocket` headers if a client connects without valid WebSocket handshake headers.

---

## The `WebSocket` Connection Object

When a client upgrades to WebSocket, an instance of `aegon::http::websocket::WebSocket` represents the active connection:

### Connection State & Properties

```cpp
// Returns true if the connection and underlying transport are active
bool active = ws.is_open();

// Access the underlying OS socket descriptor
int fd = ws.fd();

// Request path that initiated the upgrade (e.g. "/chat")
std::string_view path = ws.path();

// Underlying transport protocol (TransportProtocol::Http1 or TransportProtocol::Http3)
TransportProtocol proto = ws.transport_protocol();
```

### Sending Messages

All transmission methods return an asynchronous `aegon::core::Task<void>`:

```cpp
// 1. Send Text Frame (Opcode::Text)
co_await ws.send_text("Hello from Aegon!");

// 2. Send Binary Frame (Opcode::Binary) from std::span or container
std::vector<uint8_t> bytes = {0x01, 0x02, 0x03, 0x04};
co_await ws.send_binary(bytes);

// 3. Send Frame with explicit Opcode
co_await ws.send("custom payload", Opcode::Text);

// 4. Send Ping Control Frame (payload max 125 bytes)
co_await ws.send_ping("heartbeat");

// 5. Send Pong Control Frame (usually automatic, but can be sent manually)
co_await ws.send_pong("heartbeat");

// 6. Graceful Closing Handshake
co_await ws.close(CloseCode::Normal, "Goodbye");
```

---

## Event Callbacks & Hooks

Event handlers can be assigned dynamically to the `WebSocket` object:

### `on_text`
Invoked when a complete unmasked text frame (`Opcode::Text`) is received:
```cpp
ws.on_text([](WebSocket& ws, std::string_view text) -> aegon::core::Task<void> {
    // text is valid for the duration of this coroutine
    co_await ws.send_text(text);
});
```

### `on_binary`
Invoked when a complete unmasked binary frame (`Opcode::Binary`) is received:
```cpp
ws.on_binary([](WebSocket& ws, std::span<const uint8_t> data) -> aegon::core::Task<void> {
    // data is a non-owning span over the unmasked bytes
    co_await ws.send_binary(data);
});
```

### `on_message`
Unified callback invoked for **both** Text and Binary frames:
```cpp
ws.on_message([](WebSocket& ws, const Message& msg) -> aegon::core::Task<void> {
    if (msg.is_text()) {
        std::cout << "Text message: " << msg.text() << "\n";
    } else if (msg.is_binary()) {
        std::cout << "Binary message: " << msg.binary().size() << " bytes\n";
    }
    co_return;
});
```

### `on_ping`
Invoked when a ping frame is received. Aegon automatically transmits the RFC 6455 pong reply, and then triggers this hook:
```cpp
ws.on_ping([](WebSocket& ws, std::string_view payload) -> aegon::core::Task<void> {
    std::cout << "Ping received with payload: " << payload << "\n";
    co_return;
});
```

### `on_close`
Invoked when the client initiates a close handshake or the session closes:
```cpp
ws.on_close([](WebSocket& ws, CloseCode code, std::string_view reason) -> aegon::core::Task<void> {
    std::cout << "Session closed. Code: " << static_cast<uint16_t>(code) 
              << ", Reason: " << reason << "\n";
    co_return;
});
```

### `on_error`
Invoked if an unexpected exception occurs during connection processing:
```cpp
ws.on_error([](WebSocket& ws, const std::exception& e) {
    std::cerr << "WebSocket error on fd " << ws.fd() << ": " << e.what() << "\n";
});
```

---

## The `Message` Object

The `Message` class provides a unified interface over incoming frames:

```cpp
class Message {
public:
    // Raw payload view
    std::string_view payload() const noexcept;

    // String view of payload (for text frames)
    std::string_view text() const noexcept;

    // Byte span of payload (for binary frames)
    std::span<const uint8_t> binary() const noexcept;

    // Frame Opcode
    Opcode opcode() const noexcept;

    // Type inspection
    bool is_text() const noexcept;
    bool is_binary() const noexcept;
};
```

---

## Opcodes & Control Frames

Defined in `aegon::http::websocket::Opcode`:

| Enum | Value | Description |
|---|---|---|
| `Opcode::Continuation` | `0x0` | Continuation frame for fragmented messages. |
| `Opcode::Text` | `0x1` | UTF-8 text message. |
| `Opcode::Binary` | `0x2` | Binary data message. |
| `Opcode::Close` | `0x8` | Connection close control frame. |
| `Opcode::Ping` | `0x9` | Ping control frame. |
| `Opcode::Pong` | `0xA` | Pong control frame. |

### Helper Function
```cpp
// Returns true for Close, Ping, or Pong opcodes
bool is_ctrl = is_control_opcode(msg.opcode());
```

---

## Close Codes (`CloseCode`)

Defined in `aegon::http::websocket::CloseCode`:

| Enum | Value | Meaning |
|---|---|---|
| `CloseCode::Normal` | `1000` | Normal closure; the connection successfully completed its purpose. |
| `CloseCode::GoingAway` | `1001` | Endpoint is going away (server shutting down or browser navigating). |
| `CloseCode::ProtocolError` | `1002` | Protocol violation detected by endpoint. |
| `CloseCode::UnsupportedData` | `1003` | Data format not acceptable (e.g. binary only received text). |
| `CloseCode::NoStatusReceived` | `1005` | Reserved: No status code present in close frame. |
| `CloseCode::AbnormalClosure` | `1006` | Reserved: Connection closed abnormally without close frame. |
| `CloseCode::InvalidFramePayload` | `1007` | Inconsistent payload data (e.g. invalid UTF-8 in text frame). |
| `CloseCode::PolicyViolation` | `1008` | Generic policy violation. |
| `CloseCode::MessageTooBig` | `1009` | Message payload exceeded maximum allowed size. |
| `CloseCode::MandatoryExtension` | `1010` | Client expected an extension not negotiated. |
| `CloseCode::InternalError` | `1011` | Unexpected internal server error. |

---

## Low-Level Framing & Utilities

For developers writing custom protocol adapters or benchmarking utilities, Aegon exposes the core framing functions in `aegon::http::websocket`:

### Header Parsing
```cpp
FrameHeader header;
FrameParseResult res = parse_frame_header(buffer, header);
// Returns: FrameParseResult::Complete, FrameParseResult::NeedMoreData, or FrameParseResult::ProtocolError
```

### Header Serialization
```cpp
std::array<uint8_t, 10> out_header{};
// Returns number of header bytes written (2, 4, or 10)
size_t hlen = serialize_frame_header(Opcode::Text, payload_len, out_header.data(), /*fin=*/true);
```

### In-Place Tiered SIMD Unmasking
```cpp
// Hardware accelerated (AVX2 -> SSE2/NEON -> SWAR -> bitwise tail)
unmask_payload_inplace(payload_bytes, payload_len, mask_key);
```

### Handshake Key Calculation
```cpp
// Calculates Sec-WebSocket-Accept from Sec-WebSocket-Key
std::string accept = compute_accept_key(client_key);

// Builds the full HTTP/1.1 101 Switching Protocols response string
std::string handshake_res = build_handshake_response(accept);
```
