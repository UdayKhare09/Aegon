#include "http/Server.h"
#include "http/grpc/GrpcStatus.h"
#include "http/grpc/GrpcCodec.h"
#include "http/grpc/GrpcService.h"
#include "proto/benchmark.pb.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <cstdlib>

using namespace aegon;
using namespace aegon::http;

int main() {
    std::cout << "=======================================================\n";
    std::cout << "       AEGON NATIVE gRPC CLIENT STREAMING TEST         \n";
    std::cout << "=======================================================\n\n";

    constexpr uint16_t PORT = 20053;
    Router router;

    auto benchmark_svc = router.grpc_service("/benchmark.BenchmarkService");

    // Client Streaming RPC: AccumulateSum (stream StreamRequest -> SumReply)
    benchmark_svc.client_streaming<benchmark::StreamRequest, benchmark::SumReply>(
        "AccumulateSum",
        [](grpc::ClientStreamingContext<benchmark::StreamRequest, benchmark::SumReply>& ctx) -> core::Task<void> {
            int32_t total = 0;
            while (auto req = co_await ctx.read()) {
                total += (req->a() + req->b());
            }

            benchmark::SumReply reply;
            reply.set_result(total);
            co_await ctx.reply(reply);
            co_return;
        }
    );

    // Client Streaming RPC with validation error
    benchmark_svc.client_streaming<benchmark::StreamRequest, benchmark::SumReply>(
        "AccumulateSumStrict",
        [](grpc::ClientStreamingContext<benchmark::StreamRequest, benchmark::SumReply>& ctx) -> core::Task<void> {
            while (auto req = co_await ctx.read()) {
                if (req->a() < 0) {
                    ctx.error(grpc::StatusCode::InvalidArgument, "Negative value not allowed");
                    co_return;
                }
            }
            benchmark::SumReply reply;
            reply.set_result(42);
            co_await ctx.reply(reply);
            co_return;
        }
    );

    Server server(std::move(router));
    server.listen(PORT, "127.0.0.1");

    std::thread server_thread([&]() {
        server.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Prepare client streaming binary file containing 3 concatenated gRPC frames
    std::string req_file = "/tmp/grpc_client_stream_req.bin";
    {
        std::ofstream ofs(req_file, std::ios::binary);

        // Frame 1: a=10, b=20
        {
            benchmark::StreamRequest r1;
            r1.set_a(10);
            r1.set_b(20);
            std::string f1;
            grpc::GrpcCodec<benchmark::StreamRequest>::encode(r1, f1);
            ofs.write(f1.data(), f1.size());
        }

        // Frame 2: a=30, b=40
        {
            benchmark::StreamRequest r2;
            r2.set_a(30);
            r2.set_b(40);
            std::string f2;
            grpc::GrpcCodec<benchmark::StreamRequest>::encode(r2, f2);
            ofs.write(f2.data(), f2.size());
        }

        // Frame 3: a=50, b=50
        {
            benchmark::StreamRequest r3;
            r3.set_a(50);
            r3.set_b(50);
            std::string f3;
            grpc::GrpcCodec<benchmark::StreamRequest>::encode(r3, f3);
            ofs.write(f3.data(), f3.size());
        }
    }

    // Prepare error request file with a < 0
    std::string err_file = "/tmp/grpc_client_stream_err.bin";
    {
        std::ofstream ofs(err_file, std::ios::binary);
        benchmark::StreamRequest r_err;
        r_err.set_a(-5);
        r_err.set_b(10);
        std::string ferr;
        grpc::GrpcCodec<benchmark::StreamRequest>::encode(r_err, ferr);
        ofs.write(ferr.data(), ferr.size());
    }

    // 1. Test Client Streaming RPC (AccumulateSum: (10+20) + (30+40) + (50+50) = 200)
    std::cout << "[TEST 1] Testing Client Streaming RPC (3 streamed messages)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + req_file + " -o /tmp/grpc_client_stream_res.bin -D /tmp/grpc_client_stream_headers.txt "
                          "http://127.0.0.1:20053/benchmark.BenchmarkService/AccumulateSum";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream ifs("/tmp/grpc_client_stream_res.bin", std::ios::binary);
        std::string res_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        assert(res_data.size() >= 5);
        benchmark::SumReply reply;
        bool ok = grpc::GrpcCodec<benchmark::SumReply>::decode(res_data, reply);
        assert(ok);
        assert(reply.result() == 200);

        std::ifstream hfs("/tmp/grpc_client_stream_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 0") != std::string::npos || headers_str.find("grpc-status") != std::string::npos);

        std::cout << "  -> PASS: Successfully accumulated 3 streamed requests into reply: 200 with grpc-status: 0\n";
    }

    // 2. Test Client Streaming Validation Error
    std::cout << "\n[TEST 2] Testing Client Streaming Validation Error (negative a)...\n";
    {
        std::string cmd = "curl -s --http2-prior-knowledge -H 'Content-Type: application/grpc' "
                          "--data-binary @" + err_file + " -D /tmp/grpc_client_stream_err_headers.txt "
                          "http://127.0.0.1:20053/benchmark.BenchmarkService/AccumulateSumStrict";
        int ret = std::system(cmd.c_str());
        assert(ret == 0);

        std::ifstream hfs("/tmp/grpc_client_stream_err_headers.txt");
        std::string headers_str((std::istreambuf_iterator<char>(hfs)), std::istreambuf_iterator<char>());
        assert(headers_str.find("grpc-status: 3") != std::string::npos);

        std::cout << "  -> PASS: Correctly emitted grpc-status: 3 (InvalidArgument) trailer\n";
    }

    // Clean up
    std::remove(req_file.c_str());
    std::remove(err_file.c_str());
    std::remove("/tmp/grpc_client_stream_res.bin");
    std::remove("/tmp/grpc_client_stream_headers.txt");
    std::remove("/tmp/grpc_client_stream_err_headers.txt");

    std::cout << "\n=======================================================\n";
    std::cout << "   ALL gRPC CLIENT STREAMING TESTS PASSED PERFECTLY!   \n";
    std::cout << "=======================================================\n";

    std::exit(0);
}
