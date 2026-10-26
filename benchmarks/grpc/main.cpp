#include "http/Server.h"
#include "http/grpc/GrpcService.h"
#include "proto/benchmark.pb.h"
#include <iostream>
#include <cstdlib>

using namespace aegon;
using namespace aegon::http;

int main(int argc, char** argv) {
    uint16_t port = 50051;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    Router router;
    auto svc = router.grpc_service("/benchmark.BenchmarkService");

    // Zero-overhead synchronous unary RPC handler (0 coroutines, 0 allocations)
    svc.unary<benchmark::SumRequest, benchmark::SumReply>("GetSum", [](grpc::UnaryContext<benchmark::SumRequest, benchmark::SumReply>& ctx) {
        benchmark::SumReply reply;
        reply.set_result(ctx.req().a() + ctx.req().b());
        ctx.reply(reply);
    });

    size_t threads = 4;
    if (argc > 2) {
        threads = static_cast<size_t>(std::atoi(argv[2]));
    }

    Server server(std::move(router));
    server.tcp_nodelay(true);
    server.h2_max_concurrent_streams(2000);
    server.h2_initial_window_size(1048576);
    server.listen(port, "127.0.0.1");

    std::cout << "Aegon gRPC Benchmark Server listening on 127.0.0.1:" << port << " [threads=" << threads << "]\n";
    if (threads > 1) {
        server.run(threads);
    } else {
        server.run();
    }

    return 0;
}
