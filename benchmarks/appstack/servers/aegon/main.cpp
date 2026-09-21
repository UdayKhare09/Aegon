#include <aegon/http/Server.h>
#include <aegon/core/Task.h>
#include <iostream>
#include <cstdlib>
#include <charconv>
#include <cctype>

using namespace aegon::http;

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

#include <aegon/core/EventLoop.h>

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

int main(int argc, char* argv[]) {
    size_t threads = 2;
    uint16_t port = 18090;

    if (argc > 1) {
        threads = static_cast<size_t>(std::atoi(argv[1]));
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

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

    std::cout << "Aegon appstack benchmark listening on http://0.0.0.0:" << port << " with " << threads << " worker threads" << std::endl;
    server.listen(port);
    server.run(threads);
    return 0;
}
