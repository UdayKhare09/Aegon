#pragma once

#include "http/jwt/JwtAlgorithm.h"
#include "http/jwt/JwtDenylist.h"
#include "http/jwt/Jwks.h"
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <optional>
#include <expected>
#include <memory>
#include <chrono>
#include <functional>
#include <stdexcept>

namespace aegon::http::jwt {

/**
 * @brief Configuration options for JWT verification.
 */
struct JwtVerifierOptions {
    std::optional<std::string> issuer{};
    std::optional<std::string> audience{};
    std::optional<std::string> expected_kid{};
    int64_t leeway_seconds{0};
    std::shared_ptr<IJwtDenylist> denylist{nullptr};
    std::shared_ptr<Jwks> jwks{nullptr};
    std::function<std::optional<std::string>(std::string_view kid)> key_resolver{nullptr};
};

/**
 * @brief Successful verification result containing parsed custom claims and standard JWT claims.
 */
template <typename TClaims = void>
struct JwtResult {
    TClaims claims{};
    std::optional<std::string> issuer{};
    std::optional<std::string> audience{};
    std::optional<std::string> subject{};
    std::optional<std::string> jti{};
    std::optional<std::string> kid{};
    std::optional<int64_t> exp{};
    std::optional<int64_t> iat{};
    std::optional<int64_t> nbf{};
    std::string raw_payload{};
};

/**
 * @brief Specialization when only standard claims and raw payload are required.
 */
template <>
struct JwtResult<void> {
    std::optional<std::string> issuer{};
    std::optional<std::string> audience{};
    std::optional<std::string> subject{};
    std::optional<std::string> jti{};
    std::optional<std::string> kid{};
    std::optional<int64_t> exp{};
    std::optional<int64_t> iat{};
    std::optional<int64_t> nbf{};
    std::string raw_payload{};
};

/**
 * @brief Verifies cryptographic signature, validates standard claims, and deserializes payload.
 */
template <typename DefaultClaims = void>
class JwtVerifier {
public:
    /**
     * @brief Constructs a JwtVerifier with a static key or PEM.
     *
     * @param alg Expected algorithm (e.g. HS256, RS256, ES256).
     * @param key_or_pem For HMAC: shared secret string. For RSA/ECDSA: PEM formatted public or private key.
     * @param options Verification rules (expected issuer, audience, leeway, denylist).
     */
    JwtVerifier(Algorithm alg, std::string key_or_pem, JwtVerifierOptions options = {})
        : alg_(alg), options_(std::move(options)) {
        if (is_hmac(alg)) {
            if (key_or_pem.empty()) {
                throw std::invalid_argument("HMAC secret cannot be empty");
            }
            secret_ = std::move(key_or_pem);
        } else {
            // First attempt to load as public key
            pkey_ = load_public_key_pem(key_or_pem);
            if (!pkey_) {
                // If not in SubjectPublicKeyInfo format, try loading as private key (which contains public key material)
                pkey_ = load_private_key_pem(key_or_pem);
            }
            if (!pkey_) {
                throw std::invalid_argument("Failed to parse public key PEM for " + std::string(algorithm_to_string(alg)));
            }
        }
    }

    /**
     * @brief Constructs a JwtVerifier that dynamically resolves public keys via a JWKS or key_resolver callback.
     *
     * @param alg Expected algorithm (e.g. RS256, ES256).
     * @param options Options containing jwks or key_resolver.
     */
    JwtVerifier(Algorithm alg, JwtVerifierOptions options)
        : alg_(alg), options_(std::move(options)) {
        if (!options_.jwks && !options_.key_resolver) {
            throw std::invalid_argument("Dynamic JwtVerifier requires either a jwks or key_resolver in options");
        }
    }

    /**
     * @brief Factory creating a JwtVerifier by reading a PEM public key from a file.
     */
    [[nodiscard]] static JwtVerifier from_file(Algorithm alg, const std::string& path, JwtVerifierOptions options = {}) {
        return JwtVerifier(alg, detail::read_file_to_string(path), std::move(options));
    }

    /**
     * @brief Cryptographically verifies the token and validates claims against configuration.
     *
     * @tparam TClaims The destination struct to deserialize claims into (defaults to DefaultClaims).
     * @param token Compact JWT string (header.payload.signature).
     * @return JwtResult<TClaims> on success, or JwtError on failure.
     */
    template <typename TClaims = DefaultClaims>
    [[nodiscard]] std::expected<JwtResult<TClaims>, JwtError> verify(std::string_view token) const {
        // 1. Split into 3 parts: header.payload.signature
        size_t first_dot = token.find('.');
        if (first_dot == std::string_view::npos || first_dot == 0) {
            return std::unexpected(JwtError::MalformedToken);
        }

        size_t second_dot = token.find('.', first_dot + 1);
        if (second_dot == std::string_view::npos || second_dot <= first_dot + 1 || second_dot + 1 >= token.size()) {
            return std::unexpected(JwtError::MalformedToken);
        }

        std::string_view header_b64 = token.substr(0, first_dot);
        std::string_view payload_b64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
        std::string_view sig_b64 = token.substr(second_dot + 1);

        // 2. Decode header & verify algorithm
        auto header_json = base64url_decode(header_b64);
        if (!header_json) {
            return std::unexpected(JwtError::MalformedToken);
        }

        detail::JwtHeader h{};
        auto hdr_ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(h, *header_json);
        if (hdr_ec) {
            return std::unexpected(JwtError::MalformedToken);
        }

        auto token_alg = algorithm_from_string(h.alg);
        if (!token_alg || *token_alg != alg_) {
            return std::unexpected(JwtError::AlgorithmMismatch);
        }

        // Validate expected_kid if configured
        if (options_.expected_kid.has_value()) {
            if (h.kid.empty() || h.kid != *options_.expected_kid) {
                return std::unexpected(JwtError::KeyNotFound);
            }
        }

        // 3. Decode signature & verify crypto
        auto raw_sig = base64url_decode(sig_b64);
        if (!raw_sig) {
            return std::unexpected(JwtError::MalformedToken);
        }

        std::string_view signing_input = token.substr(0, second_dot);
        bool sig_valid = false;

        if (options_.jwks) {
            if (h.kid.empty()) {
                return std::unexpected(JwtError::KeyNotFound);
            }
            auto pkey = options_.jwks->get_key(h.kid);
            if (!pkey) {
                return std::unexpected(JwtError::KeyNotFound);
            }
            sig_valid = verify_asymmetric(alg_, pkey.get(), signing_input, *raw_sig);
        } else if (options_.key_resolver) {
            auto key_str_opt = options_.key_resolver(h.kid);
            if (!key_str_opt) {
                return std::unexpected(JwtError::KeyNotFound);
            }
            if (is_hmac(alg_)) {
                sig_valid = verify_hmac(alg_, *key_str_opt, signing_input, *raw_sig);
            } else {
                auto pkey = load_public_key_pem(*key_str_opt);
                if (!pkey) pkey = load_private_key_pem(*key_str_opt);
                if (!pkey) return std::unexpected(JwtError::KeyError);
                sig_valid = verify_asymmetric(alg_, pkey.get(), signing_input, *raw_sig);
            }
        } else if (is_hmac(alg_)) {
            sig_valid = verify_hmac(alg_, secret_, signing_input, *raw_sig);
        } else {
            sig_valid = verify_asymmetric(alg_, pkey_.get(), signing_input, *raw_sig);
        }

        if (!sig_valid) {
            return std::unexpected(JwtError::SignatureMismatch);
        }

        // 4. Decode payload
        auto payload_json = base64url_decode(payload_b64);
        if (!payload_json) {
            return std::unexpected(JwtError::MalformedToken);
        }

        // 5. Parse standard claims
        glz::generic_json<glz::num_mode::i64> doc;
        auto payload_ec = glz::read_json(doc, *payload_json);
        if (payload_ec) {
            return std::unexpected(JwtError::ParseError);
        }

        JwtResult<TClaims> result{};
        result.raw_payload = *payload_json;
        if (!h.kid.empty()) {
            result.kid = h.kid;
        }

        auto get_str = [](const auto& val) -> std::optional<std::string> {
            if (auto* s = val.template get_if<std::string>()) return *s;
            return std::nullopt;
        };

        auto get_num = [](const auto& val) -> std::optional<int64_t> {
            if (auto* p = val.template get_if<int64_t>()) return *p;
            if (auto* p = val.template get_if<double>()) return static_cast<int64_t>(*p);
            return std::nullopt;
        };

        if (doc.contains("iss")) {
            result.issuer = get_str(doc["iss"]);
        }
        if (doc.contains("aud")) {
            result.audience = get_str(doc["aud"]);
        }
        if (doc.contains("sub")) {
            if (auto s = get_str(doc["sub"])) {
                result.subject = std::move(*s);
            } else if (auto n = get_num(doc["sub"])) {
                result.subject = std::to_string(*n);
            }
        }
        if (doc.contains("jti")) {
            result.jti = get_str(doc["jti"]);
        }
        if (doc.contains("exp")) {
            result.exp = get_num(doc["exp"]);
        }
        if (doc.contains("iat")) {
            result.iat = get_num(doc["iat"]);
        }
        if (doc.contains("nbf")) {
            result.nbf = get_num(doc["nbf"]);
        }

        // 6. Validate standard claims against rules and current time
        auto now = std::chrono::system_clock::now();
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        // Not Before (nbf)
        if (result.nbf.has_value()) {
            if (now_sec + options_.leeway_seconds < *result.nbf) {
                return std::unexpected(JwtError::TokenNotYetValid);
            }
        }

        // Expiration (exp)
        if (result.exp.has_value()) {
            if (now_sec - options_.leeway_seconds >= *result.exp) {
                return std::unexpected(JwtError::TokenExpired);
            }
        }

        // Issuer (iss)
        if (options_.issuer.has_value()) {
            if (!result.issuer.has_value() || *result.issuer != *options_.issuer) {
                return std::unexpected(JwtError::IssuerMismatch);
            }
        }

        // Audience (aud)
        if (options_.audience.has_value()) {
            if (!result.audience.has_value() || *result.audience != *options_.audience) {
                return std::unexpected(JwtError::AudienceMismatch);
            }
        }

        // Replay / Denylist check
        if (options_.denylist && result.jti.has_value()) {
            if (options_.denylist->is_revoked(*result.jti)) {
                return std::unexpected(JwtError::RevokedToken);
            }
        }

        // 7. Deserialize into custom TClaims struct (if not void)
        if constexpr (!std::is_void_v<TClaims>) {
            auto claims_ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(result.claims, *payload_json);
            if (claims_ec) {
                return std::unexpected(JwtError::ParseError);
            }
        }

        return result;
    }

    [[nodiscard]] Algorithm algorithm() const noexcept { return alg_; }
    [[nodiscard]] const JwtVerifierOptions& options() const noexcept { return options_; }

private:
    Algorithm alg_;
    std::string secret_{};
    std::shared_ptr<EVP_PKEY> pkey_{nullptr};
    JwtVerifierOptions options_{};
};

} // namespace aegon::http::jwt
