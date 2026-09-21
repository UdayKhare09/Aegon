#include "http/Server.h"
#include "http/middleware/Compress.h"
#include "http/v1/Http1Parser.h"
#include <iostream>
#include <cassert>
#include <string>
#include <string_view>
#include <vector>
#include <libdeflate.h>

using namespace aegon;
using namespace aegon::http;
using namespace aegon::http::middleware;

static Response dispatch_offline(Router& router, std::string_view raw_req) {
    Request req;
    size_t consumed = 0;
    std::string s(raw_req);
    v1::Http1Parser::parse(s, req, consumed);
    Response res;
    auto task = router.dispatch(req, res, nullptr);
    task.resume();
    return res;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "    Aegon HTTP SIMD Compress Middleware Test Suite      \n";
    std::cout << "========================================================\n";

    std::string large_payload;
    for (int i = 0; i < 50; ++i) {
        large_payload += "Item " + std::to_string(i) + ": Aegon high-performance C++26 web framework\n";
    }
    assert(large_payload.size() > 500);

    // [TEST 1] Gzip compression with Accept-Encoding: gzip
    {
        std::cout << "[TEST 1] Testing gzip compression (Accept-Encoding: gzip)...\n";
        Router router;
        router.use(Compress({.min_size = 100}));
        router.get("/data", [&large_payload](Context& ctx) {
            ctx.res().status(StatusCode::Ok).text(large_payload);
        });

        Response res = dispatch_offline(router, "GET /data HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.headers().get("Content-Encoding") == "gzip");
        assert(res.headers().get("Vary") == "Accept-Encoding");
        assert(res.body().size() < large_payload.size());

        std::string decompressed;
        bool ok = middleware::gzip_decompress(res.body(), decompressed);
        assert(ok);
        (void)ok;
        assert(decompressed == large_payload);
        std::cout << "  -> PASS [gzip compressed size: " << res.body().size() << " vs original: " << large_payload.size() << "]\n";
    }

    // [TEST 2] Deflate compression with Accept-Encoding: deflate
    {
        std::cout << "[TEST 2] Testing deflate compression (Accept-Encoding: deflate)...\n";
        Router router;
        router.use(Compress({.min_size = 100}));
        router.get("/data", [&large_payload](Context& ctx) {
            ctx.res().status(StatusCode::Ok).text(large_payload);
        });

        Response res = dispatch_offline(router, "GET /data HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: deflate\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.headers().get("Content-Encoding") == "deflate");
        assert(res.headers().get("Vary") == "Accept-Encoding");
        assert(res.body().size() < large_payload.size());

        std::string decompressed;
        bool ok = middleware::deflate_decompress(res.body(), decompressed);
        assert(ok);
        (void)ok;
        assert(decompressed == large_payload);
        std::cout << "  -> PASS [deflate compressed size: " << res.body().size() << " vs original: " << large_payload.size() << "]\n";
    }

    // [TEST 3] Deflate preference when both gzip and deflate are accepted
    {
        std::cout << "[TEST 3] Testing deflate preference (Accept-Encoding: gzip, deflate)...\n";
        Router router;
        router.use(Compress({.min_size = 100, .prefer_deflate = true}));
        router.get("/data", [&large_payload](Context& ctx) {
            ctx.res().status(StatusCode::Ok).text(large_payload);
        });

        Response res = dispatch_offline(router, "GET /data HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip, deflate\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.headers().get("Content-Encoding") == "deflate");
        assert(res.headers().get("Vary") == "Accept-Encoding");
        assert(res.body().size() < large_payload.size());

        std::string decompressed;
        bool ok = middleware::deflate_decompress(res.body(), decompressed);
        assert(ok);
        (void)ok;
        assert(decompressed == large_payload);
        std::cout << "  -> PASS [deflate selected over gzip when both acceptable]\n";
    }

    // [TEST 4] Vary header merging with pre-existing Vary (e.g. CORS Origin)
    {
        std::cout << "[TEST 4] Testing safe Vary header merging...\n";
        Router router;
        router.use(Compress({.min_size = 100}));
        router.get("/cors-data", [&large_payload](Context& ctx) {
            ctx.res().header("Vary", "Origin");
            ctx.res().status(StatusCode::Ok).text(large_payload);
        });

        Response res = dispatch_offline(router, "GET /cors-data HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.headers().get("Content-Encoding") == "gzip");
        assert(res.headers().get("Vary") == "Origin, Accept-Encoding");
        std::cout << "  -> PASS [Vary merged correctly: " << *res.headers().get("Vary") << "]\n";
    }

    // [TEST 5] Bypass when body is smaller than min_size
    {
        std::cout << "[TEST 5] Testing min_size threshold bypass...\n";
        Router router;
        router.use(Compress({.min_size = 256}));
        router.get("/small", [](Context& ctx) {
            ctx.res().status(StatusCode::Ok).text("small payload");
        });

        Response res = dispatch_offline(router, "GET /small HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(!res.headers().contains("Content-Encoding"));
        assert(res.body() == "small payload");
        std::cout << "  -> PASS [payload under 256B remained uncompressed]\n";
    }

    // [TEST 6] Bypass when client does not send Accept-Encoding
    {
        std::cout << "[TEST 6] Testing bypass when client lacks Accept-Encoding...\n";
        Router router;
        router.use(Compress({.min_size = 100}));
        router.get("/data", [&large_payload](Context& ctx) {
            ctx.res().status(StatusCode::Ok).text(large_payload);
        });

        Response res = dispatch_offline(router, "GET /data HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(!res.headers().contains("Content-Encoding"));
        assert(res.body() == large_payload);
        std::cout << "  -> PASS [uncompressed plain response returned]\n";
    }

    // [TEST 7] Bypass when response already has Content-Encoding
    {
        std::cout << "[TEST 7] Testing bypass when Content-Encoding already set...\n";
        Router router;
        router.use(Compress({.min_size = 100}));
        router.get("/precompressed", [&large_payload](Context& ctx) {
            ctx.res().header("Content-Encoding", "br");
            ctx.res().status(StatusCode::Ok).text(large_payload);
        });

        Response res = dispatch_offline(router, "GET /precompressed HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
        assert(res.status() == StatusCode::Ok);
        assert(res.headers().get("Content-Encoding") == "br");
        assert(res.body() == large_payload);
        std::cout << "  -> PASS [pre-encoded response not modified]\n";
    }

    std::cout << "\nAll Aegon SIMD Compress Middleware tests passed successfully!\n";
    return 0;
}
