#pragma once

#include "http/jwt/JwtAlgorithm.h"
#include "data/uuid/UUIDGenerator.h"
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <chrono>
#include <optional>
#include <memory>
#include <stdexcept>

namespace aegon::http::jwt {

/**
 * @brief JWT Signer producing cryptographically signed tokens from C++ structs or DTOs.
 *
 * @tparam DefaultClaims Optional default claims struct type. If specified, allows calling
 *                       .sign(claims, ...) without explicit template instantiation.
 */
template <typename DefaultClaims = void>
class JwtSigner {
public:
    /**
     * @brief Constructs a JwtSigner with an algorithm and key.
     *
     * @param alg Algorithm to use (e.g. HS256, RS256, ES256).
     * @param key_or_pem For HMAC: shared secret string. For RSA/ECDSA: PEM formatted private key.
     */
    JwtSigner(Algorithm alg, std::string key_or_pem)
        : alg_(alg) {
        if (is_hmac(alg)) {
            if (key_or_pem.empty()) {
                throw std::invalid_argument("HMAC secret cannot be empty");
            }
            secret_ = std::move(key_or_pem);
        } else {
            pkey_ = load_private_key_pem(key_or_pem);
            if (!pkey_) {
                throw std::invalid_argument("Failed to parse private key PEM for " + std::string(algorithm_to_string(alg)));
            }
        }
    }

    /**
     * @brief Factory creating a JwtSigner by reading a PEM private key from a file.
     */
    [[nodiscard]] static JwtSigner from_file(Algorithm alg, const std::string& path) {
        return JwtSigner(alg, detail::read_file_to_string(path));
    }

    /**
     * @brief Signs any Glaze-serializable claims struct and returns a signed JWT compact string.
     *
     * Automatically injects standard claims (exp, iat, nbf, jti) if they are not already
     * populated in the claims struct.
     *
     * @tparam TClaims The claims struct or DTO type.
     * @param claims User claims object.
     * @param ttl Time-to-live duration (default: 24 hours). If 0 or negative, exp is omitted.
     * @param nbf Optional explicit not-before unix timestamp (defaults to current time).
     * @return Compact JWT string in header.payload.signature format.
     */
    template <typename TClaims>
    [[nodiscard]] std::string sign(
        const TClaims& claims,
        std::chrono::seconds ttl = std::chrono::hours(24),
        std::optional<int64_t> nbf = std::nullopt
    ) const {
        // 1. Build Header
        std::string header_json = "{\"alg\":\"" + std::string(algorithm_to_string(alg_)) + "\",\"typ\":\"JWT\"}";
        std::string header_b64 = base64url_encode(header_json);

        // 2. Serialize user claims
        std::string user_payload;
        auto ec = glz::write_json(claims, user_payload);
        if (ec) {
            throw std::runtime_error("Failed to serialize claims into JSON: " + glz::format_error(ec, user_payload));
        }

        // 3. Inject standard claims into generic JSON document
        glz::generic_json<glz::num_mode::i64> doc;
        auto dec_ec = glz::read_json(doc, user_payload);
        if (dec_ec) {
            throw std::runtime_error("Failed to parse serialized claims JSON: " + glz::format_error(dec_ec, user_payload));
        }

        auto now = std::chrono::system_clock::now();
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        if (!doc.contains("iat")) {
            doc["iat"] = now_sec;
        }

        if (!doc.contains("nbf")) {
            doc["nbf"] = nbf.has_value() ? *nbf : now_sec;
        }

        if (!doc.contains("exp") && ttl.count() != 0) {
            doc["exp"] = now_sec + ttl.count();
        }

        if (!doc.contains("jti")) {
            doc["jti"] = aegon::data::UUIDGenerator::v4().str();
        }

        std::string final_payload_json;
        (void)glz::write_json(doc, final_payload_json);
        std::string payload_b64 = base64url_encode(final_payload_json);

        // 4. Compute signature over header.payload
        std::string signing_input = header_b64 + "." + payload_b64;
        std::optional<std::string> raw_sig;

        if (is_hmac(alg_)) {
            raw_sig = sign_hmac(alg_, secret_, signing_input);
        } else {
            raw_sig = sign_asymmetric(alg_, pkey_.get(), signing_input);
        }

        if (!raw_sig) {
            throw std::runtime_error("Cryptographic signing failed for " + std::string(algorithm_to_string(alg_)));
        }

        return signing_input + "." + base64url_encode(*raw_sig);
    }

    /**
     * @brief Convenience overload signing default-constructed DefaultClaims.
     */
    template <typename U = DefaultClaims>
        requires (!std::is_void_v<U>)
    [[nodiscard]] std::string sign(
        std::chrono::seconds ttl = std::chrono::hours(24),
        std::optional<int64_t> nbf = std::nullopt
    ) const {
        return sign<U>(U{}, ttl, nbf);
    }

    [[nodiscard]] Algorithm algorithm() const noexcept { return alg_; }

private:
    Algorithm alg_;
    std::string secret_{};
    std::shared_ptr<EVP_PKEY> pkey_{nullptr};
};

} // namespace aegon::http::jwt
