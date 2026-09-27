#include "http/Server.h"
#include <iostream>
#include <csignal>

using namespace aegon::http;

int main() {
    std::signal(SIGPIPE, SIG_IGN);

    Server server;
    server.ws_echo("/");
    server.listen(9001, "0.0.0.0");
    server.ws_max_message_size(16 * 1024 * 1024)
          .ws_max_frame_size(16 * 1024 * 1024);

    std::cout << "Aegon Autobahn WebSocket Echo Server listening on ws://0.0.0.0:9001\n" << std::flush;
    server.run();
    return 0;
}
