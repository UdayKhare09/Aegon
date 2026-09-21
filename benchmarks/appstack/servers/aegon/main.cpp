#include <aegon/http/Server.h>
#include <aegon/core/Task.h>
#include <aegon/core/EventLoop.h>
#include <aegon/http/middleware/Compress.h>
#include <glaze/glaze.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <charconv>
#include <cctype>
#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <array>

using namespace aegon::http;

// ── Profile 1: Baseline routes ──────────────────────────────────────────────

void handle_baseline_get(Context& ctx) {
    int64_t sum = 0;
    std::string_view q = ctx.req().query();
    while (!q.empty()) {
        size_t amp = q.find('&');
        std::string_view pair = (amp != std::string_view::npos) ? q.substr(0, amp) : q;
        size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            int64_t val = 0;
            std::from_chars(pair.data() + eq + 1, pair.data() + pair.size(), val);
            sum += val;
        }
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1);
    }
    char buf[32];
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), sum);
    ctx.res().text(std::string_view(buf, p - buf));
}

void handle_baseline_post(Context& ctx) {
    int64_t sum = 0;
    std::string_view q = ctx.req().query();
    while (!q.empty()) {
        size_t amp = q.find('&');
        std::string_view pair = (amp != std::string_view::npos) ? q.substr(0, amp) : q;
        size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            int64_t val = 0;
            std::from_chars(pair.data() + eq + 1, pair.data() + pair.size(), val);
            sum += val;
        }
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1);
    }
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

// ── Profile 2: Async delay route ────────────────────────────────────────────

aegon::core::Task<void> handle_delay(Context& ctx) {
    uint64_t ms = 0;
    if (auto ms_str = ctx.req().param("ms")) {
        std::from_chars(ms_str->data(), ms_str->data() + ms_str->size(), ms);
    }
    if (ms > 0) {
        co_await aegon::core::EventLoop::current()->ring().timeout(ms * 1'000'000ULL);
    }
    char buf[32];
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), ms);
    ctx.res().text(std::string_view(buf, p - buf));
}

// ── Profile 3: JSON Serialization & Compression ─────────────────────────────

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
    bool active{false};
    std::span<const std::string> tags;
    Rating rating;
    int64_t total{0};
};

struct JsonResponsePayload {
    std::span<const ProcessedItem> items;
    size_t count{0};
};

static std::vector<DatasetItem> g_dataset;
inline thread_local std::array<ProcessedItem, 64> tls_processed_items;

static void load_dataset() {
    const char* env_path = std::getenv("DATASET_PATH");
    std::vector<std::string> candidate_paths;
    if (env_path) candidate_paths.push_back(env_path);
    candidate_paths.push_back("data/dataset.json");
    candidate_paths.push_back("benchmarks/appstack/data/dataset.json");
    candidate_paths.push_back("../data/dataset.json");
    candidate_paths.push_back("../../data/dataset.json");

    std::string content;
    for (const auto& path : candidate_paths) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            content = ss.str();
            std::cout << "[Aegon] Loaded dataset from " << path << " (" << content.size() << " bytes)" << std::endl;
            break;
        }
    }

    if (content.empty()) {
        std::cerr << "[Aegon] WARNING: Could not find dataset.json!" << std::endl;
        return;
    }

    auto err = glz::read_json(g_dataset, content);
    if (err) {
        std::cerr << "[Aegon] ERROR parsing dataset.json: " << glz::format_error(err, content) << std::endl;
    } else {
        std::cout << "[Aegon] Parsed " << g_dataset.size() << " items from dataset.json" << std::endl;
    }
}

void handle_json(Context& ctx) {
    size_t count = 50;
    if (auto c_str = ctx.req().param("count")) {
        std::from_chars(c_str->data(), c_str->data() + c_str->size(), count);
    }
    count = std::min(count, std::min(g_dataset.size(), tls_processed_items.size()));

    int64_t m = 1;
    if (auto m_val = ctx.req().query_param("m")) {
        std::from_chars(m_val->data(), m_val->data() + m_val->size(), m);
    }

    for (size_t i = 0; i < count; ++i) {
        const auto& d = g_dataset[i];
        tls_processed_items[i] = ProcessedItem{
            .id = d.id,
            .name = d.name,
            .category = d.category,
            .price = d.price,
            .quantity = d.quantity,
            .active = d.active,
            .tags = std::span<const std::string>(d.tags.data(), d.tags.size()),
            .rating = d.rating,
            .total = d.price * d.quantity * m
        };
    }

    JsonResponsePayload payload{
        .items = std::span<const ProcessedItem>(tls_processed_items.data(), count),
        .count = count
    };

    ctx.res().status(StatusCode::Ok).json(payload);
}

// ── Main Entrypoint ─────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    size_t threads = 2;
    uint16_t port = 18090;

    if (argc > 1) {
        threads = static_cast<size_t>(std::atoi(argv[1]));
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    load_dataset();

    Server server;
    server.buffer_pool_entries(16384);
    server.ring_entries(8192);

    // Profile 1: Baseline endpoints
    server.router().get("/baseline11", handle_baseline_get);
    server.router().post("/baseline11", handle_baseline_post);
    server.router().get("/baseline2", handle_baseline_get);
    server.router().post("/baseline2", handle_baseline_post);

    // Profile 2: Async delay endpoint
    server.router().get("/delay/:ms", handle_delay);

    // Profile 3: JSON & Compression endpoint (SIMD libdeflate)
    server.router().get("/json/:count", {aegon::http::middleware::Compress({.min_size = 100})}, handle_json);

    std::cout << "Aegon appstack benchmark listening on http://0.0.0.0:" << port << " with " << threads << " worker threads" << std::endl;
    server.listen(port);
    server.run(threads);
    return 0;
}
