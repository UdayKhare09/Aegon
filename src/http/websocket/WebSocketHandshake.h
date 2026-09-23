#pragma once

#include "http/Request.h"
#include <string>
#include <string_view>
#include <openssl/sha.h>
#include <array>
#include <cstdint>

namespace aegon::http::websocket {

inline constexpr std::string_view WS_MAGIC_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

inline constexpr char BASE64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Encodes raw bytes to RFC 4648 standard Base64 with padding.
 */
inline std::string base64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i < len) {
        size_t rem = len - i;
        uint32_t b0 = data[i++];
        uint32_t b1 = rem > 1 ? data[i++] : 0;
        uint32_t b2 = rem > 2 ? data[i++] : 0;

        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(BASE64_TABLE[(triple >> 18) & 0x3F]);
        out.push_back(BASE64_TABLE[(triple >> 12) & 0x3F]);
        out.push_back(rem > 1 ? BASE64_TABLE[(triple >> 6) & 0x3F] : '=');
        out.push_back(rem > 2 ? BASE64_TABLE[triple & 0x3F] : '=');
    }
    return out;
}

/**
 * @brief Computes the Sec-WebSocket-Accept token per RFC 6455 §1.3.
 */
inline std::string compute_websocket_accept(std::string_view sec_key) {
    std::string combined;
    combined.reserve(sec_key.size() + WS_MAGIC_GUID.size());
    combined.append(sec_key);
    combined.append(WS_MAGIC_GUID);

    std::array<uint8_t, SHA_DIGEST_LENGTH> digest{};
    ::SHA1(reinterpret_cast<const uint8_t*>(combined.data()), combined.size(), digest.data());

    return base64_encode(digest.data(), digest.size());
}

/**
 * @brief Validates whether an incoming HTTP request is a valid RFC 6455 WebSocket Upgrade request.
 */
inline bool is_valid_websocket_upgrade(const Request& req) noexcept {
    if (req.method() != Method::GET) {
        return false;
    }

    auto upgrade_hdr = req.headers().get("Upgrade");
    if (!upgrade_hdr || !iequals(*upgrade_hdr, "websocket")) {
        return false;
    }

    auto conn_hdr = req.headers().get("Connection");
    if (!conn_hdr) {
        return false;
    }

    // Connection header may contain multiple comma-separated tokens (e.g. "keep-alive, Upgrade")
    std::string_view conn_str = *conn_hdr;
    bool has_upgrade = false;
    while (!conn_str.empty()) {
        size_t comma = conn_str.find(',');
        std::string_view token = (comma != std::string_view::npos) ? conn_str.substr(0, comma) : conn_str;
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.remove_suffix(1);

        if (iequals(token, "Upgrade")) {
            has_upgrade = true;
            break;
        }
        if (comma == std::string_view::npos) break;
        conn_str.remove_prefix(comma + 1);
    }
    if (!has_upgrade) {
        return false;
    }

    auto ver_hdr = req.headers().get("Sec-WebSocket-Version");
    if (!ver_hdr || *ver_hdr != "13") {
        return false;
    }

    auto key_hdr = req.headers().get("Sec-WebSocket-Key");
    if (!key_hdr || key_hdr->empty()) {
        return false;
    }

    return true;
}

/**
 * @brief Constructs an HTTP/1.1 101 Switching Protocols response for WebSocket upgrade.
 */
inline void build_upgrade_response_101(std::string_view sec_key, std::string& out) {
    std::string accept = compute_websocket_accept(sec_key);
    out.clear();
    out.reserve(160);
    out.append("HTTP/1.1 101 Switching Protocols\r\n");
    out.append("Upgrade: websocket\r\n");
    out.append("Connection: Upgrade\r\n");
    out.append("Sec-WebSocket-Accept: ");
    out.append(accept);
    out.append("\r\n\r\n");
}

inline std::string compute_accept_key(std::string_view sec_key) {
    return compute_websocket_accept(sec_key);
}

inline std::string build_handshake_response(std::string_view accept_val) {
    std::string out;
    out.reserve(160);
    out.append("HTTP/1.1 101 Switching Protocols\r\n");
    out.append("Upgrade: websocket\r\n");
    out.append("Connection: Upgrade\r\n");
    out.append("Sec-WebSocket-Accept: ");
    out.append(accept_val);
    out.append("\r\n\r\n");
    return out;
}

} // namespace aegon::http::websocket
