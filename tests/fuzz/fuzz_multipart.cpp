#include "http/MultipartParser.h"
#include <cstdint>
#include <cstddef>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 65536) return 0;

    std::string_view input(reinterpret_cast<const char*>(data), size);

    // Split input: First line as Content-Type / boundary header, remainder as body
    size_t split_pos = input.find('\n');
    std::string_view content_type;
    std::string_view body;

    if (split_pos != std::string_view::npos && split_pos > 0) {
        content_type = input.substr(0, split_pos);
        if (content_type.back() == '\r') {
            content_type.remove_suffix(1);
        }
        body = input.substr(split_pos + 1);
    } else {
        // Fallback: use default synthetic content-type header
        content_type = "multipart/form-data; boundary=---------------------------fuzzboundary";
        body = input;
    }

    // 1. Fuzz boundary extraction
    auto extracted_boundary = aegon::http::MultipartParser::extract_boundary(content_type);
    (void)extracted_boundary;

    // 2. Fuzz multipart form body parsing
    auto parsed = aegon::http::MultipartParser::parse(content_type, body);
    if (parsed) {
        for (const auto& field : parsed->fields()) {
            (void)field.name;
            (void)field.value;
        }
        for (const auto& file : parsed->files()) {
            (void)file.name;
            (void)file.filename;
            (void)file.content_type;
            (void)file.data;
        }
    }

    return 0;
}
