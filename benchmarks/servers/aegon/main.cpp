#include "http/Server.h"
#include "core/Task.h"
#include "core/EventLoop.h"
#include "core/engine/IoEngine.h"
#include <glaze/glaze.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <span>
#include <array>
#include <vector>
#include <charconv>
#include <cctype>
#include <algorithm>
#include <filesystem>

using namespace aegon::http;

namespace bench {

struct Rating {
    int64_t score{0};
    int64_t count{0};
};

struct DatasetItem {
    int64_t id{0};
    std::string name;
    std::string category;
    int64_t price{0};
    int64_t quantity{0};
    bool active{false};
    std::vector<std::string> tags;
    Rating rating;
};

// Zero-copy view into dataset item with per-request total calculation
struct ProcessedItem {
    int64_t id{0};
    std::string_view name;
    std::string_view category;
    int64_t price{0};
    int64_t quantity{0};
    int64_t total{0};
    bool active{false};
    std::span<const std::string> tags;
    Rating rating;
};

struct JsonPayload {
    int count{0};
    std::span<const ProcessedItem> items;
};

} // namespace bench

using namespace bench;

namespace {

static std::vector<DatasetItem> g_dataset;
inline thread_local std::array<ProcessedItem, 128> tls_processed_items;

static void load_dataset() {
    std::vector<std::string> paths = {
        "benchmarks/data/dataset.json",
        "data/dataset.json",
        "../data/dataset.json",
        "/data/dataset.json"
    };

    std::string content;
    for (const auto& p : paths) {
        std::ifstream f(p);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            content = ss.str();
            break;
        }
    }

    if (content.empty()) {
        std::cerr << "[Aegon Unified] Warning: dataset.json not found, initializing synthetic dataset\n";
        for (int64_t i = 1; i <= 100; ++i) {
            g_dataset.push_back(DatasetItem{
                .id = i,
                .name = "Item " + std::to_string(i),
                .category = "benchmark",
                .price = 100 + i,
                .quantity = 10 + (i % 50),
                .active = (i % 2 == 0),
                .tags = {"tag1", "benchmark"},
                .rating = {.score = 95, .count = 100}
            });
        }
        return;
    }

    auto ec = glz::read_json(g_dataset, content);
    if (ec) {
        std::cerr << "[Aegon Unified] Failed to parse dataset.json: " << glz::format_error(ec, content) << "\n";
    }
}

// Zero-allocation query integer parser
inline int64_t query_int(const Context& ctx, std::string_view key) noexcept {
    int64_t val = 0;
    if (auto str = ctx.req().query_param(key)) {
        std::from_chars(str->data(), str->data() + str->size(), val);
    }
    return val;
}

inline void handle_baseline_get(Context& ctx) {
    int64_t sum = query_int(ctx, "a") + query_int(ctx, "b");
    char buf[32];
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), sum);
    ctx.res().text(std::string_view(buf, p - buf));
}

inline void handle_baseline_post(Context& ctx) {
    int64_t sum = query_int(ctx, "a") + query_int(ctx, "b");
    std::string_view body = ctx.req().body();
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.front()))) body.remove_prefix(1);
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back()))) body.remove_suffix(1);
    if (!body.empty()) {
        int64_t body_val = 0;
        auto [ptr, ec] = std::from_chars(body.data(), body.data() + body.size(), body_val);
        if (ec == std::errc()) {
            sum += body_val;
        }
    }
    char buf[32];
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), sum);
    ctx.res().text(std::string_view(buf, p - buf));
}

} // namespace

int main(int argc, char* argv[]) {
    size_t threads = 2;
    uint16_t base_port = 18080;

    if (argc > 1) {
        threads = static_cast<size_t>(std::atoi(argv[1]));
    }
    if (argc > 2) {
        base_port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    std::string cert_file = "benchmarks/certs/server.crt";
    std::string key_file = "benchmarks/certs/server.key";
    if (argc > 3) cert_file = argv[3];
    if (argc > 4) key_file = argv[4];

    if (!std::filesystem::exists(cert_file) || !std::filesystem::exists(key_file)) {
        if (std::filesystem::exists("certs/server.crt") && std::filesystem::exists("certs/server.key")) {
            cert_file = "certs/server.crt";
            key_file = "certs/server.key";
        }
    }

    load_dataset();

    Server server;
    server.ring_entries(8192);
    server.buffer_pool_entries(16384);

    // 1. HttpArena Baseline (/baseline GET and POST)
    server.router().get("/baseline", handle_baseline_get);
    server.router().post("/baseline", handle_baseline_post);

    // 2. HttpArena Pipeline (/pipeline GET)
    server.router().get("/pipeline", [](Context& ctx) {
        ctx.res().text("ok");
    });

    // 3. HttpArena Dynamic JSON (/json/:count GET)
    server.router().get("/json/:count", [](Context& ctx) {
        size_t count = 0;
        if (auto count_str = ctx.req().param("count")) {
            std::from_chars(count_str->data(), count_str->data() + count_str->size(), count);
        }
        int64_t m = 1;
        if (auto m_str = ctx.req().query_param("m")) {
            std::from_chars(m_str->data(), m_str->data() + m_str->size(), m);
        }

        size_t n = std::min(count, std::min(g_dataset.size(), tls_processed_items.size()));
        for (size_t i = 0; i < n; ++i) {
            const auto& d = g_dataset[i];
            tls_processed_items[i] = ProcessedItem{
                .id = d.id,
                .name = d.name,
                .category = d.category,
                .price = d.price,
                .quantity = d.quantity,
                .total = d.price * d.quantity * m,
                .active = d.active,
                .tags = std::span<const std::string>(d.tags.data(), d.tags.size()),
                .rating = d.rating
            };
        }

        JsonPayload payload{
            .count = static_cast<int>(n),
            .items = std::span<const ProcessedItem>(tls_processed_items.data(), n)
        };
        ctx.res().json(payload);
    });

    // 4. HttpArena Async Timer (/delay/:ms GET)
    server.router().get("/delay/:ms", [](Context& ctx) -> aegon::core::Task<void> {
        uint64_t ms = 0;
        if (auto ms_str = ctx.req().param("ms")) {
            std::from_chars(ms_str->data(), ms_str->data() + ms_str->size(), ms);
        }
        if (ms > 0) {
            co_await aegon::core::EventLoop::current()->engine().timeout(ms * 1'000'000ULL);
        }
        char buf[32];
        auto [p, _] = std::to_chars(buf, buf + sizeof(buf), ms);
        ctx.res().text(std::string_view(buf, p - buf));
    });

    // 5. HttpArena Echo (/echo POST)
    server.router().post("/echo", [](Context& ctx) {
        ctx.res().header("Content-Type", "application/octet-stream");
        ctx.res().body(std::string(ctx.req().body()));
    });

    // 6. HttpArena WebSocket Echo (/ws)
    server.router().ws("/ws");

    // Port allocation:
    // base_port     (18080): HTTP/1.1 Plain & WebSocket
    // base_port + 1 (18081): TLS 1.3 HTTP/1.1
    // base_port + 2 (18082): TLS 1.3 HTTP/2 ALPN
    uint16_t port_plain = base_port;
    uint16_t port_tls   = base_port + 1;
    uint16_t port_h2    = base_port + 2;

    server.listen(port_plain);

    bool has_certs = std::filesystem::exists(cert_file) && std::filesystem::exists(key_file);
    if (has_certs) {
        server.enable_tls(cert_file, key_file);
        server.enable_http3(false);
        server.listen_tls(port_tls);
        server.listen_tls(port_h2);
    }

    std::cout << "===================================================================\n";
    std::cout << " [Aegon Unified Server] Running with " << threads << " worker threads (HttpArena Spec)\n";
    std::cout << "   -> Plain HTTP/1.1 & WS : http://0.0.0.0:" << port_plain << "\n";
    if (has_certs) {
        std::cout << "   -> TLS 1.3 HTTP/1.1    : https://0.0.0.0:" << port_tls << "\n";
        std::cout << "   -> TLS 1.3 HTTP/2      : https://0.0.0.0:" << port_h2 << "\n";
    }
    std::cout << "===================================================================\n" << std::flush;

    server.run(threads);
    return 0;
}
