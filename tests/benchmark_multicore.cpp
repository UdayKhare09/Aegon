#include "http/Server.h"
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    const size_t threads = argc > 1 ? std::stoul(argv[1]) : 1;
    const uint16_t port = argc > 2 ? static_cast<uint16_t>(std::stoul(argv[2])) : 18080;

    aegon::http::Router router;
    router.get("/ping", [](aegon::http::Context& ctx) {
        ctx.res().text("pong");
    });

    aegon::http::Server server(std::move(router));
    server.listen(port, "127.0.0.1");

    std::cerr << "Aegon benchmark server: workers=" << threads
              << " port=" << port << "\n";
    server.run(threads);
    return 0;
}
