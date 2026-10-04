# HTTP Compression Middleware

Aegon provides a built-in, SIMD-accelerated **HTTP Compression** middleware in `#include "http/middleware/Compress.h"`. Powered by `libdeflate`, it leverages processor SIMD vector extensions (AVX2, SSE4.2, ARM NEON) to compress HTTP response bodies on the fly with industry-leading throughput and zero per-request allocation overhead.

```cpp
#include "http/middleware/Compress.h"
using namespace aegon::http::middleware;
```

---

## Quick Start

### 1. Default Compression

Attach `Compress()` globally to your router:

```cpp
Server app;

// Enable gzip and deflate compression globally
app.router().use(Compress());

app.router().get("/api/large-dataset", [](Context& ctx) {
    ctx.res().json(R"([{"id": 1, "name": "Aegon High-Performance Web Services"}, ...])");
});
```

When clients connect with `Accept-Encoding: gzip, deflate`, Aegon compresses the payload and emits:
```http
HTTP/1.1 200 OK
Content-Type: application/json; charset=utf-8
Content-Encoding: deflate
Vary: Accept-Encoding
```

---

## Configuration Options (`CompressOptions`)

Customize compression behavior using modern C++ designated initializers:

```cpp
app.router().use(Compress(CompressOptions{
    .level          = 1,      // Compression level: 1 (fastest) to 12 (max)
    .min_size       = 256,    // Minimum payload size in bytes to trigger compression
    .gzip           = true,   // Enable gzip compression (RFC 1952)
    .deflate        = true,   // Enable deflate/zlib compression (RFC 1950)
    .prefer_deflate = true,   // When both gzip and deflate are acceptable, prefer deflate
    .vary           = true    // Automatically set or merge Vary: Accept-Encoding
}));
```

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `level` | `int` | `1` | Compression level passed to `libdeflate`. Level `1` delivers near-maximum compression ratio while maintaining ultra-high throughput (>1 GB/s per core). |
| `min_size` | `size_t` | `256` | Minimum response payload size (in bytes). Payloads smaller than this threshold skip compression to avoid CPU waste where compression headers outweigh payload savings. |
| `gzip` | `bool` | `true` | Enables RFC 1952 gzip compression when accepted by the client. |
| `deflate` | `bool` | `true` | Enables RFC 1950 zlib deflate compression when accepted by the client. |
| `prefer_deflate`| `bool` | `true` | When the client's `Accept-Encoding` header accepts both `gzip` and `deflate`, deflate is preferred for reduced header overhead. |
| `vary` | `bool` | `true` | Safely sets or merges `Accept-Encoding` into the response `Vary` header to ensure HTTP proxy and browser caches do not serve compressed content to incompatible clients. |

---

## Intelligent Skip Rules

Aegon's compression middleware runs late in the onion middleware pipeline during the post-handler phase (`co_await next(ctx)`), automatically skipping payloads under the following conditions:

1. **No Client Support**: Client request omits `Accept-Encoding` or explicitly forbids supported algorithms (`Accept-Encoding: identity`).
2. **Already Encoded**: The response already contains a `Content-Encoding` header (e.g. static precompressed `.br` or `.gz` sidecars served by `static_files`).
3. **No Response Body**: Status codes that cannot carry a body per RFC 9110 (e.g., `204 No Content`, `304 Not Modified`).
4. **Under Size Threshold**: Response body size is less than `options.min_size` (default 256 bytes).

---

## Vary Header Merging

Caching proxies and CDNs require the `Vary: Accept-Encoding` header so they don't serve compressed bytes to clients that don't support them.

Aegon automatically inspects any existing `Vary` headers set by downstream handlers and safely merges `Accept-Encoding`:

```cpp
// Downstream handler sets custom Vary
ctx.res().header("Vary", "User-Agent");

// Output after Compress middleware:
// Vary: User-Agent, Accept-Encoding
```

---

## Standalone SIMD Compression Functions

In addition to the HTTP middleware, Aegon exposes the low-level SIMD compression and decompression primitives for use in custom protocols, websocket payloads, or data pipelines:

```cpp
#include "http/middleware/Compress.h"

// 1. Gzip compression & decompression
std::string original = "High-performance data stream...";
std::string compressed;
bool ok = aegon::http::middleware::gzip_compress(original, compressed, /*level=*/1);

std::string decompressed;
ok = aegon::http::middleware::gzip_decompress(compressed, decompressed);
assert(decompressed == original);

// 2. Deflate compression & decompression
std::string def_compressed;
ok = aegon::http::middleware::deflate_compress(original, def_compressed, /*level=*/1);

std::string def_decompressed;
ok = aegon::http::middleware::deflate_decompress(def_compressed, def_decompressed);
assert(def_decompressed == original);
```

---

## Zero-Allocation Architecture

Aegon avoids per-request memory allocation in the compression pipeline through:
1. **Thread-Local Compressor Pools**: `libdeflate_compressor` instances are allocated once per worker thread and reused across thousands of requests.
2. **Thread-Local Scratch Buffers**: Compression scratch buffers scale dynamically and persist within thread-local storage, eliminating heap malloc/free cycles in the critical HTTP request path.
