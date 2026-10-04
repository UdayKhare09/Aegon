# Static Files & Single-Page Applications (SPAs)

Aegon includes an ultra-fast, zero-copy static asset server and SPA hosting engine built directly into the routing core. It supports in-memory RAM caching with nanosecond disk invalidation, precompressed Brotli and Gzip sidecar negotiation, strong ETag generation with HTTP 304 conditional revalidation, and HTML5 History Mode routing for modern SPAs.

---

## Quick Start

Mount any local directory under a URL prefix on the `Router` or any `RouteGroup`:

```cpp
#include "http/Server.h"

using namespace aegon::http;

int main() {
    Server server;

    // Serve public assets from ./public at /static/*
    server.router().static_files("/static", "./public");

    server.listen(8080);
    return 0;
}
```

---

## Single-Page Application (SPA) Hosting

Modern frontend frameworks (React, Vue, Svelte, Vite, Angular, Next.js static export) utilize client-side routing (HTML5 History API). When users refresh or directly load deep URLs like `/dashboard` or `/users/42`, traditional static servers return `404 Not Found`.

Aegon's SPA mode automatically falls back to `index.html` with a `200 OK` status, allowing the client-side router to mount and render the view.

### Recommended Production Setup (Vite / React / Vue)

```cpp
server.router().static_files("/", "./dist", StaticFilesOptions{
    .spa = true,                    // Enable HTML5 History Mode fallback
    .fallback_file = "index.html",  // Entry point HTML
    .cache_control = [](std::string_view path, bool is_fallback) {
        // SPA entry point: always revalidate with server for instant deployments
        if (is_fallback || path.ends_with(".html")) {
            return "no-cache";
        }
        // Hashed assets (e.g. assets/main-D9f3a1.js): cache forever
        return "public, max-age=31536000, immutable";
    }
});
```

### Smart Asset Discrimination

Aegon does not blindly return `index.html` for all 404s. If a request targets a static asset file extension (e.g. `/assets/missing-logo.png` or `/bundle.js`) and the client does not explicitly accept HTML:
- Aegon immediately responds with **`404 Not Found`**.
- This prevents browsers from attempting to execute `index.html` as JavaScript or CSS (avoiding `SyntaxError: Unexpected token '<'`).

---

## HTTP Conditional Requests & ETags (`304 Not Modified`)

By default, Aegon automatically generates strong `ETag` headers and RFC 7231 `Last-Modified` timestamps.

When browsers and proxies revalidate cached assets:
1. Client sends `If-None-Match: "..."` or `If-Modified-Since: "..."`.
2. Aegon checks the in-memory cache or file metadata.
3. If the resource has not changed, Aegon responds with **`304 Not Modified`** and an empty response body, eliminating unnecessary bandwidth consumption.

```http
GET /assets/style.css HTTP/1.1
Host: localhost:8080
If-None-Match: "0x6700142b-0x1a4"

HTTP/1.1 304 Not Modified
ETag: "0x6700142b-0x1a4"
Last-Modified: Sun, 04 Oct 2026 14:20:00 GMT
Cache-Control: public, max-age=31536000, immutable
```

---

## Precompressed Sidecars (`.br` and `.gz`)

For maximum production performance without on-the-fly CPU overhead, build tools like Vite, Webpack, and Rollup can pre-compress assets into `.br` (Brotli) and `.gz` (Gzip) sidecars during the build step:

```text
dist/
  assets/
    index-A91c.js
    index-A91c.js.br
    index-A91c.js.gz
```

When `.precompressed = true` (the default):
1. If the client sends `Accept-Encoding: br, gzip`, Aegon serves `index-A91c.js.br` with `Content-Encoding: br`.
2. If the client only supports gzip, Aegon serves `index-A91c.js.gz` with `Content-Encoding: gzip`.
3. If no precompressed sidecar exists, Aegon falls back to the uncompressed asset.

---

## In-Memory RAM Caching with `mtime` Revalidation

When `.cache_in_memory = true` (default: `true`), Aegon caches file data and metadata in RAM protected by reader-writer locks (`std::shared_mutex`).

- **Zero Disk I/O on Hit**: Cached requests are served directly from RAM without touching the filesystem.
- **Instant Hot-Reloading**: Every request rechecks the file's nanosecond modification timestamp (`st_mtim.tv_nsec`). If a file on disk is modified or replaced, Aegon automatically purges the stale cache entry and serves fresh content on the very next request.

---

## Configuration Reference (`StaticFilesOptions`)

```cpp
struct StaticFilesOptions {
    bool precompressed{true};
    bool cache_in_memory{true};
    std::string index_file{"index.html"};

    // SPA Support
    bool spa{false};
    std::string fallback_file{"index.html"};

    // HTTP Caching & Conditional Revalidation
    bool etag{true};
    bool last_modified{true};
    std::function<std::string(std::string_view path, bool is_fallback)> cache_control{nullptr};
};
```

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `precompressed` | `bool` | `true` | Negotiates precompressed `.br` and `.gz` sidecars if present on disk. |
| `cache_in_memory` | `bool` | `true` | Caches file buffers in RAM with nanosecond `mtime` invalidation. |
| `index_file` | `std::string` | `"index.html"` | Default file served when a client requests a directory path. |
| `spa` | `bool` | `false` | Enables SPA HTML5 History Mode routing. Missing non-asset paths serve `fallback_file` with `200 OK`. |
| `fallback_file` | `std::string` | `"index.html"` | Entry file served for SPA client-side routes. |
| `etag` | `bool` | `true` | Generates strong `ETag` headers and evaluates `If-None-Match`. |
| `last_modified` | `bool` | `true` | Sends `Last-Modified` timestamps and evaluates `If-Modified-Since`. |
| `cache_control` | `std::function` | `nullptr` | Custom callback `(path, is_fallback)` to assign fine-grained `Cache-Control` headers. |

---

## Supported MIME Types

Aegon automatically infers the `Content-Type` for common web, media, and SPA asset extensions:

| Extension | Content-Type |
| :--- | :--- |
| `.html`, `.htm` | `text/html; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.js`, `.mjs` | `application/javascript; charset=utf-8` |
| `.json` | `application/json; charset=utf-8` |
| `.map` | `application/json; charset=utf-8` (Source Maps) |
| `.webmanifest`, `.manifest` | `application/manifest+json; charset=utf-8` (PWA Manifests) |
| `.svg` | `image/svg+xml` |
| `.png` | `image/png` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.webp` | `image/webp` |
| `.avif` | `image/avif` |
| `.ico` | `image/x-icon` |
| `.woff2` | `font/woff2` |
| `.woff` | `font/woff` |
| `.ttf` | `font/ttf` |
| `.otf` | `font/otf` |
| `.wasm` | `application/wasm` |
| `.mp4` | `video/mp4` |
| `.webm` | `video/webm` |
| `.pdf` | `application/pdf` |
| `.xml` | `application/xml; charset=utf-8` |
| `.txt` | `text/plain; charset=utf-8` |

---

## Security & Path Traversal Guards

Aegon applies strict security controls before accessing the underlying filesystem:
- Any requested path containing `..`, `\`, or null bytes is immediately rejected with `404 Not Found`.
- Leading slashes and redundant path separators are canonicalized.
- Only regular files (`S_ISREG`) are served, preventing accidental exposure of device nodes or directories.
