#include "http/Server.h"
#include "http/SseStream.h"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <arpa/inet.h>
#include <unistd.h>

using namespace aegon::http;

struct MarketTick {
    std::string symbol;
    double price{0.0};
    double change{0.0};
    uint64_t seq{0};
};

struct SystemMetric {
    std::string host;
    int cpu_percent{0};
    int memory_mb{0};
    std::string status;
};

static std::atomic<bool> g_stop{false};

void sig_handler(int) {
    g_stop = true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    uint16_t port_plain = 8080;
    uint16_t port_tls   = 8443;

    if (argc >= 2) {
        port_plain = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    if (argc >= 3) {
        port_tls = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    std::cout << "========================================================\n";
    std::cout << "       AEGON LIVE REAL-TIME SSE SHOWCASE APP           \n";
    std::cout << "   HTTP/1.1 (Chunked), HTTP/2 (DATA), HTTP/3 (QUIC)     \n";
    std::cout << "========================================================\n";
    std::cout << " Plaintext (H1/H2C): http://127.0.0.1:" << port_plain << "\n";
    std::cout << " TLS & H3 (HTTPS):   https://127.0.0.1:" << port_tls << "\n\n";

    Router router;

    // 1. Web UI Dashboard
    router.get("/", [](Context& ctx) {
        std::string html = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Aegon SSE Real-Time Monitor</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0d1117; color: #c9d1d9; margin: 0; padding: 2rem; }
    h1 { color: #58a6ff; margin-bottom: 0.5rem; }
    .badge { display: inline-block; padding: 0.25rem 0.6rem; border-radius: 999px; font-size: 0.85rem; font-weight: bold; background: #238636; color: #fff; }
    .card-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 1.5rem; margin-top: 1.5rem; }
    .card { background: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 1.25rem; }
    .card h2 { font-size: 1.15rem; color: #f0f6fc; margin-top: 0; }
    pre { background: #090d13; padding: 0.75rem; border-radius: 6px; font-size: 0.85rem; color: #7ee787; max-height: 250px; overflow-y: auto; }
  </style>
</head>
<body>
  <h1>Aegon Server-Sent Events (SSE)</h1>
  <p><span class="badge" id="conn-badge">Connecting...</span> <span id="proto">Protocols: HTTP/1.1, HTTP/2, HTTP/3</span></p>
  <div class="card-grid">
    <div class="card">
      <h2>Live Market Ticker (/events/ticks)</h2>
      <pre id="ticks-log"></pre>
    </div>
    <div class="card">
      <h2>System Telemetry (/events/system)</h2>
      <pre id="system-log"></pre>
    </div>
  </div>
  <script>
    function stream(url, elId) {
      const el = document.getElementById(elId);
      const es = new EventSource(url);
      es.onmessage = e => { el.textContent += e.data + '\n'; el.scrollTop = el.scrollHeight; };
      es.addEventListener('tick', e => { el.textContent += '[TICK] ' + e.data + '\n'; el.scrollTop = el.scrollHeight; });
      es.addEventListener('telemetry', e => { el.textContent += '[METRIC] ' + e.data + '\n'; el.scrollTop = el.scrollHeight; });
      es.onopen = () => { document.getElementById('conn-badge').textContent = 'Live SSE Connected'; };
    }
    stream('/events/ticks?count=10', 'ticks-log');
    stream('/events/system?count=10', 'system-log');
  </script>
</body>
</html>)raw";
        ctx.res().html(html);
    });

    // 2. Health Check (Non-SSE regression)
    router.get("/health", [](Context& ctx) {
        ctx.res().status(StatusCode::Ok).json(R"({"status":"healthy","server":"aegon","sse":true})");
    });

    // 3. Live Market Ticks SSE
    router.get("/events/ticks", [](Context& ctx) -> aegon::core::Task<void> {
        auto stream = co_await ctx.sse();
        int count = 5;
        if (auto c_str = ctx.req().query("count")) {
            try { count = std::clamp(std::stoi(std::string(*c_str)), 1, 100); } catch (...) {}
        }

        co_await stream.comment("market ticker stream initialized");
        co_await stream.retry(1500);

        for (int i = 1; i <= count; ++i) {
            MarketTick tick{
                .symbol = (i % 2 == 0 ? "AAPL" : "NVDA"),
                .price = 150.0 + (i * 2.5),
                .change = (i % 2 == 0 ? +1.45 : -0.85),
                .seq = static_cast<uint64_t>(i)
            };
            co_await stream.send_json("tick", tick);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        co_await stream.event("status", "stream_completed");
        stream.close();
    });

    // 4. Live System Metrics SSE
    router.get("/events/system", [](Context& ctx) -> aegon::core::Task<void> {
        auto stream = co_await ctx.sse();
        int count = 5;
        if (auto c_str = ctx.req().query("count")) {
            try { count = std::clamp(std::stoi(std::string(*c_str)), 1, 100); } catch (...) {}
        }

        co_await stream.comment("system telemetry feed");

        for (int i = 1; i <= count; ++i) {
            SystemMetric metric{
                .host = "edge-node-01",
                .cpu_percent = 15 + (i * 3),
                .memory_mb = 512 + (i * 16),
                .status = "nominal"
            };
            co_await stream.send_json("telemetry", metric);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        stream.close();
    });

    // 5. Raw Event Stream
    router.get("/events/raw", [](Context& ctx) -> aegon::core::Task<void> {
        auto stream = co_await ctx.sse();
        co_await stream.event("notice", "welcome to aegon raw sse");
        co_await stream.data("line 1: real-time streaming data");
        co_await stream.data("line 2: multiplexed without buffering");
        co_await stream.comment("heartbeat-keepalive");
        stream.close();
    });

    Server server(std::move(router));
    server.listen(port_plain, "0.0.0.0", false)
          .listen_tls(port_tls, "0.0.0.0")
          .enable_tls()
          .enable_http3();

    std::thread server_thread([&]() {
        server.run();
    });

    std::cout << "Server running. Press Ctrl+C or send SIGTERM to terminate.\n";

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nStopping server...\n";
    server.stop();

    // Trigger local wakeups to exit accept loops
    {
        int fd1 = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a1{};
        a1.sin_family = AF_INET;
        a1.sin_port = htons(port_plain);
        inet_pton(AF_INET, "127.0.0.1", &a1.sin_addr);
        connect(fd1, (sockaddr*)&a1, sizeof(a1));
        close(fd1);

        int fd2 = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a2{};
        a2.sin_family = AF_INET;
        a2.sin_port = htons(port_tls);
        inet_pton(AF_INET, "127.0.0.1", &a2.sin_addr);
        connect(fd2, (sockaddr*)&a2, sizeof(a2));
        close(fd2);
    }

    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "Server shutdown complete.\n";
    return 0;
}
