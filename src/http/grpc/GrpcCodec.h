#pragma once

#include "http/grpc/GrpcStatus.h"
#include <concepts>
#include <span>
#include <string>
#include <string_view>
#include <cstring>
#include <arpa/inet.h>

namespace aegon::http::grpc {

/**
 * @brief C++23 concept matching Google Protobuf Message classes (Message and MessageLite).
 */
template <typename T>
concept ProtobufMessage = requires(T msg, const uint8_t* data, size_t size) {
    { msg.ParseFromArray(data, static_cast<int>(size)) } -> std::same_as<bool>;
    { msg.ByteSizeLong() } -> std::convertible_to<size_t>;
    { msg.SerializeToArray(std::declval<uint8_t*>(), static_cast<int>(size)) } -> std::same_as<bool>;
};

/**
 * @brief Codec for gRPC message encoding and decoding with standard 5-byte HTTP/2 framing.
 *
 * Wire format for each gRPC message:
 *   [1 byte: compression flag (0 = none, 1 = compressed)]
 *   [4 bytes: big-endian uint32 payload length]
 *   [N bytes: serialized protobuf payload]
 */
template <typename T>
struct GrpcCodec {
    /**
     * @brief Decodes a single 5-byte framed gRPC wire payload into typed message T.
     */
    static bool decode(std::string_view raw_frame, T& out) {
        if (raw_frame.size() < 5) [[unlikely]] {
            return false;
        }

        uint8_t compressed = static_cast<uint8_t>(raw_frame[0]);
        if (compressed != 0) [[unlikely]] {
            // Compressed gRPC frames deferred to Phase 2
            return false;
        }

        uint32_t net_len = 0;
        std::memcpy(&net_len, raw_frame.data() + 1, 4);
        uint32_t msg_len = ntohl(net_len);

        if (raw_frame.size() < 5 + msg_len) [[unlikely]] {
            return false;
        }

        if constexpr (ProtobufMessage<T>) {
            return out.ParseFromArray(
                reinterpret_cast<const uint8_t*>(raw_frame.data() + 5),
                static_cast<int>(msg_len)
            );
        } else {
            return false;
        }
    }

    /**
     * @brief Encodes a typed message T into a 5-byte framed gRPC wire payload buffer.
     */
    static bool encode(const T& in, std::string& out_buf) {
        if constexpr (ProtobufMessage<T>) {
            size_t size = in.ByteSizeLong();
            out_buf.resize(5 + size);
            out_buf[0] = 0; // Uncompressed flag

            uint32_t net_len = htonl(static_cast<uint32_t>(size));
            std::memcpy(&out_buf[1], &net_len, 4);

            return in.SerializeToArray(
                reinterpret_cast<uint8_t*>(&out_buf[5]),
                static_cast<int>(size)
            );
        } else {
            return false;
        }
    }
};

} // namespace aegon::http::grpc
