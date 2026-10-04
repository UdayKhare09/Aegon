# Logging System

Aegon features a standalone, high-performance telemetry and logging library (`aegon_log`) built from the ground up for modern C++26 and Linux systems. It operates independently at the base of the framework, enabling structured, leveled logging across the core engine, server lifecycle, and user applications.

---

## Key Features

- **C++26 Native**: Compile-time format string validation via `std::format_string<Args...>`. Format mismatches are caught at compile time.
- **Zero-Macro Requirement**: Automatically captures the caller's filename, line number, and function using `std::source_location::current()` as a default argument.
- **Zero Allocation on Filtered Levels**: Log levels below the active threshold perform a single atomic check and return immediately with zero string formatting or memory allocation.
- **Asynchronous & Non-Blocking**: Optional background worker thread with a bounded queue and double-buffered batch flushing so HTTP request workers are never blocked on disk or terminal I/O.
- **Crash Safety**: `Error` and `Fatal` log levels trigger an immediate synchronous flush to ensure diagnostics are persisted even during unexpected shutdowns.
- **Pluggable Sinks**: Ships with colorized `ConsoleSink`, append-optimized `FileSink`, and in-memory `MemorySink` for unit testing.

---

## Quick Start

```cpp
#include <log/Logger.h>

// Clean functional syntax (source location captured automatically, no macros needed!)
aegon::log::info("Server listening on {}:{}", "0.0.0.0", 8080);
aegon::log::warn("Buffer pool low: {} buffers available", 16);
aegon::log::error("Connection error: {}", std::strerror(errno));
aegon::log::debug("Direct file descriptor registered: slot={}", 4);
aegon::log::trace("Received {} bytes", 128);

// Raw strings with braces (e.g. JSON, SQL) bypass formatting safely
aegon::log::info("{\"status\": \"ok\", \"healthy\": true}");
```

### Traditional Macro Syntax

For codebases preferring uppercase macros or compile-time pruning, standard macros are also provided:

```cpp
AEGON_LOG_INFO("Processing user id={}", 42);
AEGON_LOG_WARN("Rate limit warning for IP {}", ip);
AEGON_LOG_ERROR("Database query failed: {}", err);
```

---

## Log Severity Levels

Aegon provides seven severity levels defined in `aegon::log::Level`:

| Level | Value | Color | Description |
| :--- | :--- | :--- | :--- |
| `Trace` | `0` | Dim Gray | High-volume granular protocol frames and packet traces |
| `Debug` | `1` | Cyan | Diagnostic details, buffer registrations, socket operations |
| `Info`  | `2` | Green | General operational milestones (startup, port binds, lifecycle) |
| `Warn`  | `3` | Yellow | Recoverable conditions (buffer ring fallback, high latency) |
| `Error` | `4` | Red | Critical failures (socket bind failure, uncaught task exception) |
| `Fatal` | `5` | Bold Red | Unrecoverable errors preceding abort or shutdown |
| `Off`   | `6` | — | Disables all logging output entirely |

You can dynamically adjust the global log level at runtime:

```cpp
aegon::log::set_level(aegon::log::Level::Debug);
```

---

## Initialization & Lifecycle

Calling `aegon::log::init()` is **completely optional**. If omitted, the logger lazily initializes on the first log call with default settings (`Level::Info`, Synchronous, `ConsoleSink`).

`init()` is thread-safe and can be called from anywhere at any time:

```cpp
#include <log/Logger.h>

int main() {
    // Configure high-performance asynchronous logging
    aegon::log::init({
        .level = aegon::log::Level::Info,
        .async = true,               // Run writer on background thread
        .flush_interval_ms = 10,     // Flush batch every 10ms
        .max_queue_size = 65536      // Bounded queue prevents memory exhaustion
    });

    // ... Run Aegon Server ...

    // Flush and cleanly stop background worker on shutdown
    aegon::log::shutdown();
    return 0;
}
```

---

## Sinks

Sinks implement the `ISink` interface and can be attached to the logger.

### 1. ConsoleSink

The default sink. Formats logs with timestamps, levels, file basenames, and line numbers. Auto-detects whether `stdout` is a terminal to enable high-contrast ANSI colors.

```cpp
#include <log/ConsoleSink.h>

auto console = std::make_shared<aegon::log::ConsoleSink>(aegon::log::ConsoleSinkConfig{
    .color_mode = aegon::log::ColorMode::Auto, // Auto, Always, Never
    .show_location = true,                    // Include [filename.cpp:line]
    .show_thread_id = false,                  // Include [t:thread_id]
    .stderr_for_errors = false                // Route Error/Fatal to stderr
});

aegon::log::add_sink(console);
```

### 2. FileSink

Appends log records to a designated file. Automatically creates parent directories if they do not exist.

```cpp
#include <log/FileSink.h>

// Simple path
aegon::log::add_sink(std::make_shared<aegon::log::FileSink>("logs/server.log"));

// Or with configuration
aegon::log::add_sink(std::make_shared<aegon::log::FileSink>(aegon::log::FileSinkConfig{
    .path = "logs/aegon.log",
    .append = true,
    .show_location = true,
    .show_thread_id = false
}));
```

### 3. MemorySink

Captures log records and formatted messages in a thread-safe in-memory vector. Essential for unit tests and telemetry assertions.

```cpp
#include <log/MemorySink.h>
#include <cassert>

auto mem_sink = std::make_shared<aegon::log::MemorySink>();
aegon::log::set_sinks({mem_sink});

aegon::log::info("Order #{} confirmed", 1001);

assert(mem_sink->size() == 1);
assert(mem_sink->contains("Order #1001 confirmed"));
```

---

## HTTP Access Log Middleware

Aegon provides a built-in HTTP access logging middleware in `#include <http/middleware/AccessLog.h>` to log every inbound HTTP request across HTTP/1.1, HTTP/2, and HTTP/3. For the complete reference, see the dedicated [Access Log Middleware Guide](/guide/access-log).

### Basic Usage

Attach the middleware globally to your router:

```cpp
#include <http/middleware/AccessLog.h>

router.use(aegon::http::middleware::access_log());
// Or using the semantic alias:
// router.use(aegon::http::middleware::logger());
```

By default, access logs format as:
```text
[2026-10-04 19:10:00.123] [INFO ] [AccessLog.h:120] GET /api/v1/users?page=1 200 OK 1.45ms - 127.0.0.1 (1.2 KB) [HTTP/2.0]
```

### Full Customization Options

The middleware accepts an `AccessLogConfig` struct with rich tuning options:

```cpp
#include <http/middleware/AccessLog.h>

using namespace aegon::http::middleware;

router.use(access_log(AccessLogConfig{
    // 1. Output Format (Combined, Common, Json, or Custom)
    .format = AccessLogFormat::Combined,

    // 2. Severity level mapping per status code class
    .normal_level       = aegon::log::Level::Info,  // 1xx, 2xx, 3xx
    .client_error_level = aegon::log::Level::Warn,  // 4xx
    .server_error_level = aegon::log::Level::Error, // 5xx

    // 3. Slow Request Highlighting (logs with [SLOW] tag at slow_level)
    .slow_threshold = std::chrono::milliseconds(50),
    .slow_level     = aegon::log::Level::Warn,

    // 4. Field Inclusion Flags
    .log_query    = true,  // Append ?query if present
    .log_client_ip = true,  // Extract from x-forwarded-for or x-real-ip
    .log_bytes    = true,  // Append response payload size
    .log_protocol = true,  // Append [HTTP/1.1], [HTTP/2.0], or [HTTP/3.0]

    // 5. Selected Headers to Capture and Log
    .request_headers  = {"x-request-id", "user-agent"},
    .response_headers = {"content-type"},

    // 6. Path-based Filtering (skips logging matching exact paths)
    .skip_paths = {"/health", "/healthz", "/metrics", "/favicon.ico"},

    // 7. Dynamic Predicate Filtering (return true to skip logging)
    .skip_if = [](const aegon::http::Context& ctx) {
        return ctx.req().header("x-internal-probe") == "true";
    },

    // 8. Custom Formatter (overrides standard output format)
    .custom_formatter = [](const aegon::http::Context& ctx, std::chrono::nanoseconds duration) {
        return std::format("{} {} -> {} ({}µs)",
            to_string(ctx.req().method()),
            ctx.req().path(),
            ctx.res().status_code(),
            duration.count() / 1000);
    }
}));
```

### Format Styles

- **`AccessLogFormat::Combined`** (Default):
  `GET /search?q=aegon 200 OK 1.20ms - 203.0.113.195 (4.2 KB) [HTTP/2.0] [x-request-id: abc]`
- **`AccessLogFormat::Common`** (Apache/CLF-inspired):
  `203.0.113.195 - "GET /search?q=aegon HTTP/2.0" 200 4300 (1.20ms)`
- **`AccessLogFormat::Json`** (Structured ndjson for Cloud/Datadog/Loki):
  `{"method":"GET","path":"/search","status":200,"phrase":"OK","duration_ns":1200450,"bytes":4300}`
- **`AccessLogFormat::Custom`**:
  Uses your provided `custom_formatter` function.

---

## Core Engine Telemetry

Aegon's subsystems use `aegon_log` internally to provide deep operational visibility without external overhead:

- **Server Lifecycle**: Server boot, configured listeners, HTTP/HTTPS/QUIC endpoints, and graceful termination.
- **EventLoop**: CPU core affinity pinning (`pthread_setaffinity_np`), loop execution boundaries, and uncaught root task exceptions.
- **IoUring Subsystem**: Ring file descriptor setup, kernel submission flags (`IORING_SETUP_DEFER_TASKRUN`, `SINGLE_ISSUER`), and direct file descriptor registrations.
- **BufferPool**: Hugepage/mmap buffer allocations, buffer ring registration with `io_uring`, and recovery fallbacks.

To enable verbose internal telemetry during development or debugging, simply set the log level to `Debug` or `Trace`:

```cpp
aegon::log::set_level(aegon::log::Level::Debug);
```

---

## CMake Integration

`aegon_log` is packaged as an independent CMake target and alias:

```cmake
target_link_libraries(my_app PRIVATE Aegon::log Aegon::core)
```
