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

    Server server(std::move(router));
    server.listen(port, "127.0.0.1");

    std::cout << "Aegon gRPC Benchmark Server listening on 127.0.0.1:" << port << "\n";
    server.run();

    return 0;
}
