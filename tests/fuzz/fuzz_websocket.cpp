#include "http/websocket/WebSocketFrame.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string_view>
#include <algorithm>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 65536) return 0;

    std::string_view frame_bytes(reinterpret_cast<const char*>(data), size);

    aegon::http::websocket::FrameHeader header{};
    auto status = aegon::http::websocket::parse_frame_header(frame_bytes, header);

    if (status == aegon::http::websocket::FrameParseResult::Complete) {
        (void)header.fin;
        (void)header.opcode;
        (void)header.payload_len;
        (void)header.mask_key;

        // If payload is available in this input slice, exercise SIMD tiered in-place unmasking
        if (header.header_len < size && header.masked) {
            size_t available_payload = std::min(static_cast<size_t>(header.payload_len), size - header.header_len);
            if (available_payload > 0) {
                std::vector<uint8_t> payload_copy(data + header.header_len, data + header.header_len + available_payload);
                aegon::http::websocket::unmask_payload_inplace(payload_copy.data(), payload_copy.size(), header.mask_key);
            }
        }
    }

    return 0;
}
