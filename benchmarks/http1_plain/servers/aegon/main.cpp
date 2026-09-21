#include <aegon/http/Server.h>
#include <aegon/core/Task.h>
#include <aegon/data/types/UUID.h>
#include <glaze/glaze.hpp>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <charconv>

using namespace aegon::http;

struct JsonMessage {
    std::string message = "Hello, World!";
    std::string id;
    uint64_t timestamp = 0;
};

struct PostInfo {
    uint64_t user_id = 0;
    uint64_t post_id = 0;
};

int main(int argc, char* argv[]) {
    size_t threads = 4;
    uint16_t port = 18080;

    if (argc > 1) {
        threads = static_cast<size_t>(std::atoi(argv[1]));
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    Server server;

    // 1. Plaintext benchmark (TechEmpower standard)
    server.router().get("/plaintext", [](Context& ctx) {
        ctx.res().text("Hello, World!");
    });

    // 2. JSON Serialization benchmark with dynamic UUID & timestamp
    server.router().get("/json", [](Context& ctx) {
        JsonMessage msg{
            .message = "Hello, World!",
            .id = aegon::data::UUIDGenerator::v4().to_string(),
            .timestamp = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            )
        };
        ctx.res().json(msg);
    });

    // 3. Dynamic route with two URL path parameters
    server.router().get("/users/:id/posts/:post_id", [](Context& ctx) {
        PostInfo info;
        if (auto u = ctx.req().param("id")) {
            std::from_chars(u->data(), u->data() + u->size(), info.user_id);
        }
        if (auto p = ctx.req().param("post_id")) {
            std::from_chars(p->data(), p->data() + p->size(), info.post_id);
        }
        ctx.res().json(info);
    });

    server.listen(port);
    std::cout << "Aegon listening on http://0.0.0.0:" << port << " with " << threads << " worker threads" << std::endl;
    server.run(threads);
    return 0;
}
