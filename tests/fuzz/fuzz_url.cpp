#include "http/client/Url.h"
#include <cstdint>
#include <cstddef>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 8192) return 0; // Cap URL length

    std::string_view raw(reinterpret_cast<const char*>(data), size);

    auto parsed = aegon::http::client::Url::parse(raw);
    if (parsed) {
        (void)parsed->scheme();
        (void)parsed->host();
        (void)parsed->port();
        (void)parsed->path();
        (void)parsed->query();
        (void)parsed->fragment();
        (void)parsed->target();
        (void)parsed->host_header();
        (void)parsed->is_https();
    }

    return 0;
}
