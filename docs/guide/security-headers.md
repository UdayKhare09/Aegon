# Security Headers Middleware

Aegon provides a production-grade, zero-overhead **SecurityHeaders** middleware establishing an OWASP-compliant HTTP defense-in-depth baseline to protect against common web attacks (clickjacking, MIME type confusion, cross-window leakage, and unauthorized caching).

```cpp
#include "http/middleware/SecurityHeaders.h"
using namespace aegon::http::middleware;
```

---

## Quick Start

### 1. Default OWASP Baseline (Web Applications)

Attach to the server globally to apply recommended OWASP defaults:

```cpp
Server app;

app.use(security_headers()); // SecurityHeadersConfig::defaults()
```

Emits the following headers automatically:
- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `X-XSS-Protection: 0`
- `Referrer-Policy: strict-origin-when-cross-origin`
- `Strict-Transport-Security: max-age=31536000; includeSubDomains`
- `Cross-Origin-Opener-Policy: same-origin`
- `Cross-Origin-Resource-Policy: same-origin`

### 2. Headless REST / JSON API Baseline

For pure JSON backends that serve no HTML or iframes:

```cpp
app.use(security_headers(SecurityHeadersConfig::api()));
```

---

## Configuration Reference (`SecurityHeadersConfig`)

```cpp
struct SecurityHeadersConfig {
    bool content_type_nosniff{true};
    FrameOption frame_options{FrameOption::Deny};
    bool xss_protection_disabled{true};
    ReferrerPolicy referrer_policy{ReferrerPolicy::StrictOriginWhenCrossOrigin};
    HstsConfig hsts{};
    std::string content_security_policy{};
    bool csp_report_only{false};
    std::string permissions_policy{};
    CrossOriginOpenerPolicy coop{CrossOriginOpenerPolicy::SameOrigin};
    CrossOriginEmbedderPolicy coep{CrossOriginEmbedderPolicy::Disabled};
    CrossOriginResourcePolicy corp{CrossOriginResourcePolicy::SameOrigin};
    bool prevent_browser_caching{false};
};
```

### Options Breakdown

| Field | Type | Default | Description |
|---|---|---|---|
| `content_type_nosniff` | `bool` | `true` | Emits `X-Content-Type-Options: nosniff` to prevent browsers from MIME-sniffing away from declared `Content-Type`. |
| `frame_options` | `FrameOption` | `Deny` | Clickjacking defense (`X-Frame-Options: DENY` or `SAMEORIGIN`). Set to `Disabled` if using CSP `frame-ancestors`. |
| `xss_protection_disabled` | `bool` | `true` | Emits `X-XSS-Protection: 0`. Modern OWASP standard disabling legacy buggy browser XSS auditors. |
| `referrer_policy` | `ReferrerPolicy` | `StrictOriginWhenCrossOrigin` | Controls path/query leakage in cross-origin HTTP `Referer` headers. |
| `hsts` | `HstsConfig` | 1 Year + Subdomains | HTTP Strict Transport Security (`max-age=31536000; includeSubDomains; preload`). |
| `content_security_policy` | `std::string` | `""` | Restricts sources of scripts, styles, images, and fonts (`Content-Security-Policy`). |
| `csp_report_only` | `bool` | `false` | Emits `Content-Security-Policy-Report-Only` for testing policies without blocking resources. |
| `permissions_policy` | `std::string` | `""` | Restricts browser hardware features (`camera=(), microphone=(), geolocation=()`). |
| `coop` | `CrossOriginOpenerPolicy` | `SameOrigin` | Cross-Origin-Opener-Policy. Isolates window browsing context against Spectre attacks. |
| `coep` | `CrossOriginEmbedderPolicy` | `Disabled` | Cross-Origin-Embedder-Policy (`require-corp` or `credentialless`). |
| `corp` | `CrossOriginResourcePolicy` | `SameOrigin` | Cross-Origin-Resource-Policy. Blocks cross-origin reads of your resources. |
| `prevent_browser_caching` | `bool` | `false` | Emits zero-cache headers to protect sensitive/authenticated routes against disk caching. |

---

## Enumeration Reference

Every security enum in Aegon maps directly to standardized RFC / W3C HTTP header values.

### 1. `FrameOption` (`X-Frame-Options`)

Defines whether the browser is allowed to render a page in a `<frame>`, `<iframe>`, `<embed>`, or `<object>` to prevent clickjacking:

| Enum Value | Header Emitted | Behavior |
|---|---|---|
| `FrameOption::Deny` | `X-Frame-Options: DENY` | **Recommended default.** Completely prohibits framing by any site, including self. |
| `FrameOption::SameOrigin` | `X-Frame-Options: SAMEORIGIN` | Only permits framing by pages originating from the exact same origin. |
| `FrameOption::Disabled` | *(header omitted)* | Omits the header entirely (recommended if you use modern CSP `frame-ancestors`). |

---

### 2. `ReferrerPolicy` (`Referrer-Policy`)

Controls how much referrer information (URL path and query parameters) is included in requests made from your site:

| Enum Value | Header Value | Description |
|---|---|---|
| `ReferrerPolicy::StrictOriginWhenCrossOrigin` | `strict-origin-when-cross-origin` | **Default.** Sends full path same-origin, sends only origin (no path/query) cross-origin HTTPS, sends nothing on downgrade to HTTP. |
| `ReferrerPolicy::NoReferrer` | `no-referrer` | Never sends the `Referer` header under any circumstance. |
| `ReferrerPolicy::NoReferrerWhenDowngrade` | `no-referrer-when-downgrade` | Sends full URL to HTTPS, never sends to HTTP. |
| `ReferrerPolicy::SameOrigin` | `same-origin` | Sends referrer for same-origin requests only; cross-origin sends nothing. |
| `ReferrerPolicy::Origin` | `origin` | Always sends only the origin (scheme + host + port), omitting path and query. |
| `ReferrerPolicy::StrictOrigin` | `strict-origin` | Sends only origin to HTTPS destinations; sends nothing on downgrade to HTTP. |
| `ReferrerPolicy::OriginWhenCrossOrigin` | `origin-when-cross-origin` | Sends full URL same-origin; sends only origin cross-origin. |
| `ReferrerPolicy::UnsafeUrl` | `unsafe-url` | Always sends full URL including path and query (insecure; not recommended). |

---

### 3. `CrossOriginOpenerPolicy` (`Cross-Origin-Opener-Policy` / COOP)

Isolates your browsing context to prevent cross-window attacks (such as Spectre or `window.opener` manipulation):

| Enum Value | Header Value | Description |
|---|---|---|
| `CrossOriginOpenerPolicy::SameOrigin` | `same-origin` | **Default.** Isolates the browsing context exclusively to same-origin documents. Cross-origin popups cannot reference `window.opener`. |
| `CrossOriginOpenerPolicy::SameOriginAllowPopups` | `same-origin-allow-popups` | Retains references to popups that don't set COOP or set `unsafe-none`. |
| `CrossOriginOpenerPolicy::UnsafeNone` | `unsafe-none` | Allows the document to be added to its opener's browsing context group. |
| `CrossOriginOpenerPolicy::Disabled` | *(header omitted)* | Omits the COOP header. |

---

### 4. `CrossOriginEmbedderPolicy` (`Cross-Origin-Embedder-Policy` / COEP)

Controls whether cross-origin subresources (scripts, images, stylesheets) must explicitly grant permission to be loaded:

| Enum Value | Header Value | Description |
|---|---|---|
| `CrossOriginEmbedderPolicy::RequireCorp` | `require-corp` | A resource can only be loaded if its response includes a `Cross-Origin-Resource-Policy` header or CORS approval. *(Required together with COOP to enable `SharedArrayBuffer`)*. |
| `CrossOriginEmbedderPolicy::Credentialless` | `credentialless` | Loads cross-origin resources without credentials (cookies), avoiding the need for CORP headers. |
| `CrossOriginEmbedderPolicy::UnsafeNone` | `unsafe-none` | Resources can be loaded without explicit permission. |
| `CrossOriginEmbedderPolicy::Disabled` | *(header omitted)* | **Default.** Omits the COEP header. |

---

### 5. `CrossOriginResourcePolicy` (`Cross-Origin-Resource-Policy` / CORP)

Protects your server's static files, APIs, and media from being read or embedded by unauthorized cross-origin sites:

| Enum Value | Header Value | Description |
|---|---|---|
| `CrossOriginResourcePolicy::SameOrigin` | `same-origin` | **Default.** Only requests from the exact same origin can read this resource. |
| `CrossOriginResourcePolicy::SameSite` | `same-site` | Only requests from the same site (e.g. `*.example.com`) can read this resource. |
| `CrossOriginResourcePolicy::CrossOrigin` | `cross-origin` | Any origin can read or embed this resource (useful for public CDNs/assets). |
| `CrossOriginResourcePolicy::Disabled` | *(header omitted)* | Omits the CORP header. |

---

## Advanced Security Controls

### 1. Sensitive Route Cache Prevention (`prevent_browser_caching`)

For authenticated or sensitive API endpoints, Aegon can instruct browsers and intermediate reverse proxies never to persist responses to disk or shared caches. 

When enabled:
```cpp
app.use(security_headers(SecurityHeadersConfig{
    .prevent_browser_caching = true
}));
```

Aegon automatically emits:
```http
Cache-Control: no-cache, no-store, max-age=0, must-revalidate
Pragma: no-cache
Expires: 0
```

### 2. HTTP Strict Transport Security (HSTS) Customization

HSTS forces browsers to communicate strictly over HTTPS:

```cpp
using namespace std::chrono_literals;

app.use(security_headers(SecurityHeadersConfig{
    .hsts = HstsConfig{
        .enabled = true,
        .max_age = 7300h,            // ~10 months
        .include_subdomains = true,   // Applies to *.yourdomain.com
        .preload = true               // For Google Chrome HSTS Preload List
    }
}));
```

### 3. Content Security Policy (CSP)

Define trusted execution boundaries:

```cpp
app.use(security_headers(SecurityHeadersConfig{
    .content_security_policy = "default-src 'self'; script-src 'self' https://cdn.trusted.com; img-src * data:",
    .csp_report_only = false // Set to true to test without enforcement
}));
```

### 4. Frame Embedding (Clickjacking Protection)

```cpp
// 1. Completely deny framing (Default)
app.use(security_headers(SecurityHeadersConfig{
    .frame_options = FrameOption::Deny
}));

// 2. Allow only same-origin framing
app.use(security_headers(SecurityHeadersConfig{
    .frame_options = FrameOption::SameOrigin
}));

// 3. Omit header (when relying on CSP frame-ancestors)
app.use(security_headers(SecurityHeadersConfig{
    .frame_options = FrameOption::Disabled
}));
```

---

## Combining with CORS

`security_headers` and `cors` are designed to work together seamlessly:

```cpp
Server app;

// Global security pipeline
app.use(security_headers(SecurityHeadersConfig::defaults()))
   .use(cors(CorsConfig::strict({"https://admin.example.com"})));

app.router().get("/dashboard", [](Context& ctx) {
    ctx.res().json(R"({"status":"ok"})");
});
```
