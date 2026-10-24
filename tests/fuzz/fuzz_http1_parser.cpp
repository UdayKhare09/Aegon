#include "http/v1/Http1Parser.h"
#include "http/Request.h"
#include "http/Response.h"
#include <cstdint>
#include <cstddef>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 65536) return 0; // Cap to 64KB per iteration

    std::string_view input(reinterpret_cast<const char*>(data), size);

    // 1. Fuzz HTTP/1.1 Request parser
    {
        aegon::http::Request req;
        size_t consumed = 0;
        auto st = aegon::http::v1::Http1Parser::parse(input, req, consumed);
        if (st == aegon::http::v1::ParseStatus::Complete) {
            (void)req.method();
            (void)req.path();
            (void)req.query();
            (void)req.body();
            for (const auto& [name, val] : req.headers()) {
                (void)name;
                (void)val;
            }
        }
    }

    // 2. Fuzz HTTP/1.1 Response parser
    {
        aegon::http::Response res;
        size_t consumed = 0;
        auto st = aegon::http::v1::Http1Parser::parse_response(input, res, consumed);
        if (st == aegon::http::v1::ParseStatus::Complete) {
            (void)res.status_code();
            (void)res.body();
            for (const auto& [name, val] : res.headers()) {
                (void)name;
                (void)val;
            }
        }
    }

    return 0;
}
