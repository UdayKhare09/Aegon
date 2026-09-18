# HTTP Response

Inside every handler, you construct the outgoing HTTP response using `ctx.res()`. 

The `Response` object features a fluent, chainable API designed for zero-copy performance, compile-time JSON serialization, and zero-syscall static file serving.

---

## Setting Status Codes

You can set HTTP status codes using either the strongly-typed `StatusCode` enum or a raw integer:

```cpp
// Strongly-typed StatusCode enum
ctx.res().status(StatusCode::Ok);                   // 200
ctx.res().status(StatusCode::Created);              // 201
ctx.res().status(StatusCode::NoContent);            // 204
ctx.res().status(StatusCode::BadRequest);           // 400
ctx.res().status(StatusCode::NotFound);             // 404
ctx.res().status(StatusCode::UnprocessableEntity);  // 422
ctx.res().status(StatusCode::InternalServerError);  // 500

// Raw uint16_t numeric code
ctx.res().status(418); // I'm a teapot
```

Each status code automatically maps to standard RFC 9110 status phrases (e.g. `status_phrase(StatusCode::Ok)` yields `"OK"`).

---

## Sending Content

`Response` provides specialized methods that set the response body and its corresponding `Content-Type` header in a single fluent call:

### Plain Text (`.text()`)

```cpp
ctx.res().text("Hello, World!");
// Sets Content-Type: text/plain; charset=utf-8
```

### Raw JSON String (`.json(string_view)`)

```cpp
ctx.res().json(R"({"status":"ready","uptime_sec":120})");
// Sets Content-Type: application/json; charset=utf-8
```

### Typed C++ Struct Serialization (`.json<T>(const T&)`)

Using Aegon's integrated Glaze serializer, C++ structs and standard containers can be serialized directly into the response buffer without intermediate string copies:

```cpp
struct Product {
    uint64_t id;
    std::string name;
    double price;
};

server.router().get("/product", [](Context& ctx) -> Task<void> {
    Product p{.id = 42, .name = "Mechanical Keyboard", .price = 149.99};
    ctx.res().json(p); // Serialized directly via Glaze
    co_return;
});
```

### HTML (`.html()`)

```cpp
ctx.res().html("<h1>Welcome to Aegon</h1><p>High performance Linux async engine.</p>");
// Sets Content-Type: text/html; charset=utf-8
```

### Raw Binary / Custom Body (`.body()`)

```cpp
ctx.res()
   .header("Content-Type", "application/octet-stream")
   .body(binary_payload);
```

---

## Custom Response Headers

### String View Headers (`.header()`)

When your header name and value are string literals or have lifetimes spanning the request duration:

```cpp
ctx.res()
   .header("X-Server-Engine", "Aegon-io_uring")
   .header("Cache-Control", "public, max-age=3600")
   .text("Cached content");
```

### Dynamically Allocated Headers (`.set_header_owned()`)

When header values are dynamically generated strings (such as timestamps, computed hashes, or formatted IDs), use `set_header_owned` to transfer string ownership safely to the `Response` lifetime:

```cpp
std::string request_id = generate_uuid();
std::string timestamp  = get_rfc1123_date();

ctx.res()
   .set_header_owned("X-Request-ID", std::move(request_id))
   .set_header_owned("Date", std::move(timestamp))
   .text("Processed");
```

---

## Static File Serving

Aegon supports high-speed static file serving backed by Linux kernel zero-copy mechanisms:

```cpp
server.router().get("/download/report", [](Context& ctx) -> Task<void> {
    ctx.res().file("/var/data/annual_report.pdf");
    co_return;
});
```

### Automatic MIME Type Detection

If no MIME type is explicitly provided, `res.file()` automatically infers the content type using `Response::infer_mime_type(path)`:

| Extension | Content-Type |
| :--- | :--- |
| `.html`, `.htm` | `text/html; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.js`, `.mjs` | `application/javascript; charset=utf-8` |
| `.json` | `application/json; charset=utf-8` |
| `.png`, `.jpg`, `.gif`, `.svg`, `.ico` | `image/png`, `image/jpeg`, `image/gif`, `image/svg+xml`, `image/x-icon` |
| `.pdf`, `.wasm`, `.xml`, `.txt` | `application/pdf`, `application/wasm`, `application/xml`, `text/plain` |
| *(unknown)* | `application/octet-stream` |

If the target file does not exist on disk, `file()` automatically sets the status code to `StatusCode::NotFound`.

---

## Streaming & Chunked Transfer Encoding

For real-time feeds, Server-Sent Events (SSE), or large streamable payloads, enable HTTP chunked transfer:

```cpp
ctx.res().chunked();
// Sets Transfer-Encoding: chunked
```

You can format individual chunks according to RFC 9112 §7.1 using static helpers:

```cpp
std::string buffer;
Response::serialize_chunk("data: {\"event\":\"tick\"}\n\n", buffer);
Response::serialize_chunk_end(buffer); // Appends final 0\r\n\r\n
```
