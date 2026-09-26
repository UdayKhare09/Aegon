#include "http/Server.h"
#include <iostream>
#include <cstdlib>

using namespace aegon::http;

int main(int argc, char* argv[]) {
    Router router;
    router.get("/", [](Context& ctx) {
        ctx.res().text("Hello, Spec!");
    });
    router.post("/", [](Context& ctx) {
        ctx.res().text(ctx.req().body());
    });
    router.get("/health", [](Context& ctx) {
        ctx.res().text("OK");
    });
    router.post("/echo", [](Context& ctx) {
        ctx.res().text(ctx.req().body());
    });

    uint16_t h2c_port = 18082;
    uint16_t tls_port = 18443;
    if (argc > 1) h2c_port = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2) tls_port = static_cast<uint16_t>(std::atoi(argv[2]));

    Server server(std::move(router));
    server.listen(h2c_port, "127.0.0.1", false)
          .enable_tls()
          .listen(tls_port, "127.0.0.1", true)
          .enable_http3(true);

    std::cout << "Spec server listening on:" << std::endl;
    std::cout << "  - h2c (plaintext): http://127.0.0.1:" << h2c_port << std::endl;
    std::cout << "  - h2  (TLS):       https://127.0.0.1:" << tls_port << std::endl;
    std::cout << "  - h3  (QUIC/UDP):  quic://127.0.0.1:" << tls_port << std::endl;

    server.run();
    return 0;
}
