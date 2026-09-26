#include "http/Server.h"
#include <iostream>
#include <cstdlib>

using namespace aegon::http;

int main(int argc, char* argv[]) {
    Router router;
    auto echo_handler = [](Context& ctx) {
        std::string out;
        for (const auto& h : ctx.req().headers()) {
            out.append(h.name);
            out.append(": ");
            out.append(h.value);
            out.push_back('\n');
        }
        ctx.res().header("Content-Type", "text/plain");
        ctx.res().text(out);
    };

    auto cookie_handler = [](Context& ctx) {
        auto cookie_hdr = ctx.req().headers().get("cookie");
        std::string out;
        if (cookie_hdr) {
            std::string_view raw = *cookie_hdr;
            size_t pos = 0;
            while (pos < raw.size()) {
                size_t semi = raw.find(';', pos);
                std::string_view pair = raw.substr(pos, semi == std::string_view::npos ? raw.size() - pos : semi - pos);
                size_t start = pair.find_first_not_of(" \t");
                if (start != std::string_view::npos) {
                    pair.remove_prefix(start);
                    size_t eq = pair.find('=');
                    if (eq != std::string_view::npos && eq > 0) {
                        out.append(pair.substr(0, eq));
                        out.push_back('=');
                        out.append(pair.substr(eq + 1));
                        out.push_back('\n');
                    }
                }
                if (semi == std::string_view::npos) break;
                pos = semi + 1;
            }
        }
        ctx.res().header("Content-Type", "text/plain");
        ctx.res().text(out);
    };

    auto root_and_catchall_handler = [](Context& ctx) {
        ctx.res().header("Content-Type", "text/plain");
        if (ctx.req().method() == Method::POST) {
            ctx.res().text(ctx.req().body());
        } else {
            ctx.res().text("OK");
        }
    };

    router.all("/echo", echo_handler);
    router.all("/cookie", cookie_handler);
    router.all("/", root_and_catchall_handler);
    router.set_not_found_handler(root_and_catchall_handler);
    router.get("/health", [](Context& ctx) {
        ctx.res().text("OK");
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
