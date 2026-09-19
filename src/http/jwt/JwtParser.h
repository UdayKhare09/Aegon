#pragma once

#include "http/jwt/JwtAlgorithm.h"
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <optional>

namespace aegon::http::jwt {

/**
 * @brief High-performance raw JWT parser without cryptographic verification.
 *
 * Useful for debugging, tracing, and API gateway routing where signature
 * verification was already performed at edge proxies.
 */
class JwtParser {
public:
    /**
     * @brief Decodes the raw JSON header of a JWT token.
     */
    [[nodiscard]] static std::optional<std::string> decode_header(std::string_view token) {
        size_t first_dot = token.find('.');
        if (first_dot == std::string_view::npos || first_dot == 0) {
            return std::nullopt;
        }
        return base64url_decode(token.substr(0, first_dot));
    }

    /**
     * @brief Decodes the raw JSON payload of a JWT token.
     */
    [[nodiscard]] static std::optional<std::string> decode_payload_json(std::string_view token) {
        size_t first_dot = token.find('.');
        if (first_dot == std::string_view::npos) return std::nullopt;

        size_t second_dot = token.find('.', first_dot + 1);
        if (second_dot == std::string_view::npos || second_dot <= first_dot + 1) {
            return std::nullopt;
        }

        std::string_view payload_b64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
        return base64url_decode(payload_b64);
    }

    /**
     * @brief Extracts the Algorithm declared in the JWT header.
     */
    [[nodiscard]] static std::optional<Algorithm> get_algorithm(std::string_view token) {
        auto header_opt = decode_header(token);
        if (!header_opt) return std::nullopt;

        detail::JwtHeader h{};
        auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(h, *header_opt);
        if (ec) return std::nullopt;

        return algorithm_from_string(h.alg);
    }

    /**
     * @brief Deserializes the JWT payload into a typed C++ struct or DTO without verifying signature.
     */
    template <typename TClaims>
    [[nodiscard]] static std::optional<TClaims> decode_payload(std::string_view token) {
        auto payload_opt = decode_payload_json(token);
        if (!payload_opt) return std::nullopt;

        TClaims claims{};
        auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(claims, *payload_opt);
        if (ec) return std::nullopt;

        return claims;
    }
};

} // namespace aegon::http::jwt
