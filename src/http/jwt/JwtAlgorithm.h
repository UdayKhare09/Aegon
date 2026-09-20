#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <expected>
#include <chrono>
#include <memory>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>

namespace aegon::http::jwt {

/**
 * @brief Supported cryptographic algorithms for JWT signing and verification (RFC 7518).
 */
enum class Algorithm {
    HS256,
    HS384,
    HS512,
    RS256,
    RS384,
    RS512,
    PS256,
    PS384,
    PS512,
    ES256,
    ES384,
    ES512
};

/**
 * @brief Converts an Algorithm enum to its standard JWA string representation.
 */
[[nodiscard]] constexpr std::string_view algorithm_to_string(Algorithm alg) noexcept {
    switch (alg) {
        case Algorithm::HS256: return "HS256";
        case Algorithm::HS384: return "HS384";
        case Algorithm::HS512: return "HS512";
        case Algorithm::RS256: return "RS256";
        case Algorithm::RS384: return "RS384";
        case Algorithm::RS512: return "RS512";
        case Algorithm::PS256: return "PS256";
        case Algorithm::PS384: return "PS384";
        case Algorithm::PS512: return "PS512";
        case Algorithm::ES256: return "ES256";
        case Algorithm::ES384: return "ES384";
        case Algorithm::ES512: return "ES512";
    }
    return "UNKNOWN";
}

/**
 * @brief Parses an Algorithm from its standard JWA string representation.
 */
[[nodiscard]] inline std::optional<Algorithm> algorithm_from_string(std::string_view str) noexcept {
    if (str == "HS256") return Algorithm::HS256;
    if (str == "HS384") return Algorithm::HS384;
    if (str == "HS512") return Algorithm::HS512;
    if (str == "RS256") return Algorithm::RS256;
    if (str == "RS384") return Algorithm::RS384;
    if (str == "RS512") return Algorithm::RS512;
    if (str == "PS256") return Algorithm::PS256;
    if (str == "PS384") return Algorithm::PS384;
    if (str == "PS512") return Algorithm::PS512;
    if (str == "ES256") return Algorithm::ES256;
    if (str == "ES384") return Algorithm::ES384;
    if (str == "ES512") return Algorithm::ES512;
    return std::nullopt;
}

/**
 * @brief Checks if algorithm is symmetric HMAC.
 */
[[nodiscard]] constexpr bool is_hmac(Algorithm alg) noexcept {
    return alg == Algorithm::HS256 || alg == Algorithm::HS384 || alg == Algorithm::HS512;
}

/**
 * @brief Checks if algorithm is RSA PKCS#1 v1.5.
 */
[[nodiscard]] constexpr bool is_rsa(Algorithm alg) noexcept {
    return alg == Algorithm::RS256 || alg == Algorithm::RS384 || alg == Algorithm::RS512;
}

/**
 * @brief Checks if algorithm is RSA-PSS.
 */
[[nodiscard]] constexpr bool is_rsa_pss(Algorithm alg) noexcept {
    return alg == Algorithm::PS256 || alg == Algorithm::PS384 || alg == Algorithm::PS512;
}

/**
 * @brief Checks if algorithm is ECDSA.
 */
[[nodiscard]] constexpr bool is_ecdsa(Algorithm alg) noexcept {
    return alg == Algorithm::ES256 || alg == Algorithm::ES384 || alg == Algorithm::ES512;
}

/**
 * @brief Error codes returned during JWT processing.
 */
enum class JwtError {
    MalformedToken,
    AlgorithmMismatch,
    SignatureMismatch,
    TokenExpired,
    TokenNotYetValid,
    IssuerMismatch,
    AudienceMismatch,
    ParseError,
    KeyError,
    KeyNotFound,
    RevokedToken
};

/**
 * @brief User-friendly description of a JwtError.
 */
[[nodiscard]] constexpr std::string_view to_string(JwtError error) noexcept {
    switch (error) {
        case JwtError::MalformedToken: return "Token structure is malformed or invalid";
        case JwtError::AlgorithmMismatch: return "Algorithm does not match token header";
        case JwtError::SignatureMismatch: return "Cryptographic signature verification failed";
        case JwtError::TokenExpired: return "Token has expired (exp claim)";
        case JwtError::TokenNotYetValid: return "Token is not yet valid (nbf claim)";
        case JwtError::IssuerMismatch: return "Issuer mismatch (iss claim)";
        case JwtError::AudienceMismatch: return "Audience mismatch (aud claim)";
        case JwtError::ParseError: return "Failed to deserialize JSON payload into claims";
        case JwtError::KeyError: return "Invalid cryptographic key or key format";
        case JwtError::KeyNotFound: return "Key specified by kid was not found in JWKS or key resolver";
        case JwtError::RevokedToken: return "Token has been revoked (jti claim in denylist)";
    }
    return "Unknown JWT error";
}

// -----------------------------------------------------------------------------
// Base64 and Base64URL Encoders / Decoders (RFC 4648)
// -----------------------------------------------------------------------------

namespace detail {

struct JwtHeader {
    std::string alg;
    std::string typ{"JWT"};
    std::string kid{};
};

inline constexpr char BASE64URL_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

} // namespace detail

/**
 * @brief Base64URL encodes data without padding characters (RFC 4648 §5).
 */
[[nodiscard]] inline std::string base64url_encode(std::string_view in) {
    std::string out;
    size_t len = in.size();
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b0 = static_cast<uint8_t>(in[i]);
        uint32_t b1 = (i + 1 < len) ? static_cast<uint8_t>(in[i + 1]) : 0;
        uint32_t b2 = (i + 2 < len) ? static_cast<uint8_t>(in[i + 2]) : 0;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(detail::BASE64URL_CHARS[(triple >> 18) & 0x3F]);
        out.push_back(detail::BASE64URL_CHARS[(triple >> 12) & 0x3F]);
        if (i + 1 < len) out.push_back(detail::BASE64URL_CHARS[(triple >> 6) & 0x3F]);
        if (i + 2 < len) out.push_back(detail::BASE64URL_CHARS[triple & 0x3F]);
    }
    return out;
}

[[nodiscard]] inline std::string base64url_encode(const uint8_t* data, size_t len) {
    return base64url_encode(std::string_view(reinterpret_cast<const char*>(data), len));
}

/**
 * @brief Decodes a Base64 or Base64URL encoded string (accepts both padded and unpadded).
 */
[[nodiscard]] inline std::optional<std::string> base64url_decode(std::string_view in) {
    while (!in.empty() && (in.back() == '=' || in.back() == ' ' || in.back() == '\r' || in.back() == '\n' || in.back() == '\t')) {
        in.remove_suffix(1);
    }
    while (!in.empty() && (in.front() == ' ' || in.front() == '\r' || in.front() == '\n' || in.front() == '\t')) {
        in.remove_prefix(1);
    }
    if (in.empty()) return std::string{};

    static constexpr auto B64_TABLE = []() consteval {
        std::array<int8_t, 256> table{};
        table.fill(-1);
        for (uint8_t i = 0; i < 26; ++i) {
            table[static_cast<size_t>('A' + i)] = static_cast<int8_t>(i);
            table[static_cast<size_t>('a' + i)] = static_cast<int8_t>(26 + i);
        }
        for (uint8_t i = 0; i < 10; ++i) {
            table[static_cast<size_t>('0' + i)] = static_cast<int8_t>(52 + i);
        }
        table[static_cast<size_t>('+')] = 62;
        table[static_cast<size_t>('-')] = 62;
        table[static_cast<size_t>('/')] = 63;
        table[static_cast<size_t>('_')] = 63;
        table[static_cast<size_t>('=')] = -2;
        return table;
    }();

    const size_t in_len = in.size();
    const size_t out_len = (in_len * 3) / 4;
    std::string out;
    out.reserve(out_len + 3);

    size_t i = 0;
    for (; i + 4 <= in_len; i += 4) {
        int8_t a = B64_TABLE[static_cast<uint8_t>(in[i])];
        int8_t b = B64_TABLE[static_cast<uint8_t>(in[i + 1])];
        int8_t c = B64_TABLE[static_cast<uint8_t>(in[i + 2])];
        int8_t d = B64_TABLE[static_cast<uint8_t>(in[i + 3])];
        if ((a | b | c | d) < 0) return std::nullopt;

        uint32_t triple = (static_cast<uint32_t>(a) << 18) |
                          (static_cast<uint32_t>(b) << 12) |
                          (static_cast<uint32_t>(c) << 6)  |
                           static_cast<uint32_t>(d);

        out.push_back(static_cast<char>((triple >> 16) & 0xFF));
        out.push_back(static_cast<char>((triple >> 8) & 0xFF));
        out.push_back(static_cast<char>(triple & 0xFF));
    }

    size_t remainder = in_len - i;
    if (remainder == 2) {
        int8_t a = B64_TABLE[static_cast<uint8_t>(in[i])];
        int8_t b = B64_TABLE[static_cast<uint8_t>(in[i + 1])];
        if ((a | b) < 0) return std::nullopt;
        out.push_back(static_cast<char>((a << 2) | (b >> 4)));
    } else if (remainder == 3) {
        int8_t a = B64_TABLE[static_cast<uint8_t>(in[i])];
        int8_t b = B64_TABLE[static_cast<uint8_t>(in[i + 1])];
        int8_t c = B64_TABLE[static_cast<uint8_t>(in[i + 2])];
        if ((a | b | c) < 0) return std::nullopt;
        out.push_back(static_cast<char>((a << 2) | (b >> 4)));
        out.push_back(static_cast<char>(((b & 0x0F) << 4) | (c >> 2)));
    } else if (remainder != 0) {
        return std::nullopt;
    }

    return out;
}

/**
 * @brief Standard Base64 decode for HTTP Basic Auth and generic credentials.
 */
[[nodiscard]] inline std::optional<std::string> base64_decode(std::string_view in) {
    return base64url_decode(in);
}

inline constexpr char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Standard Base64 encode for HTTP Basic Auth and binary payloads.
 */
[[nodiscard]] inline std::string base64_encode(std::string_view in) {
    std::string out;
    size_t len = in.size();
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b0 = static_cast<uint8_t>(in[i]);
        uint32_t b1 = (i + 1 < len) ? static_cast<uint8_t>(in[i + 1]) : 0;
        uint32_t b2 = (i + 2 < len) ? static_cast<uint8_t>(in[i + 2]) : 0;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(BASE64_CHARS[(triple >> 18) & 0x3F]);
        out.push_back(BASE64_CHARS[(triple >> 12) & 0x3F]);
        if (i + 1 < len) out.push_back(BASE64_CHARS[(triple >> 6) & 0x3F]);
        else out.push_back('=');
        if (i + 2 < len) out.push_back(BASE64_CHARS[triple & 0x3F]);
        else out.push_back('=');
    }
    return out;
}

// -----------------------------------------------------------------------------
// OpenSSL Key Management & Cryptographic Primitives
// -----------------------------------------------------------------------------

namespace detail {

inline std::string read_file_to_string(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open key file: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline const EVP_MD* get_evp_md(Algorithm alg) noexcept {
    switch (alg) {
        case Algorithm::HS256:
        case Algorithm::RS256:
        case Algorithm::PS256:
        case Algorithm::ES256:
            return EVP_sha256();
        case Algorithm::HS384:
        case Algorithm::RS384:
        case Algorithm::PS384:
        case Algorithm::ES384:
            return EVP_sha384();
        case Algorithm::HS512:
        case Algorithm::RS512:
        case Algorithm::PS512:
        case Algorithm::ES512:
            return EVP_sha512();
    }
    return nullptr;
}

inline size_t get_ecdsa_raw_sig_length(Algorithm alg) noexcept {
    switch (alg) {
        case Algorithm::ES256: return 64;  // 32 R + 32 S
        case Algorithm::ES384: return 96;  // 48 R + 48 S
        case Algorithm::ES512: return 132; // 66 R + 66 S
        default: return 0;
    }
}

/**
 * @brief Converts OpenSSL DER ECDSA_SIG to raw R || S byte sequence (RFC 7515 §3.4).
 */
inline std::optional<std::string> ecdsa_der_to_raw(const uint8_t* der_data, size_t der_len, size_t expected_raw_len) {
    const unsigned char* p = der_data;
    ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(der_len));
    if (!sig) return std::nullopt;

    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig, &r, &s);
    if (!r || !s) {
        ECDSA_SIG_free(sig);
        return std::nullopt;
    }

    size_t half_len = expected_raw_len / 2;
    std::string raw(expected_raw_len, '\0');
    if (BN_bn2binpad(r, reinterpret_cast<unsigned char*>(raw.data()), static_cast<int>(half_len)) != static_cast<int>(half_len) ||
        BN_bn2binpad(s, reinterpret_cast<unsigned char*>(raw.data() + half_len), static_cast<int>(half_len)) != static_cast<int>(half_len)) {
        ECDSA_SIG_free(sig);
        return std::nullopt;
    }

    ECDSA_SIG_free(sig);
    return raw;
}

/**
 * @brief Converts raw R || S byte sequence into OpenSSL DER ECDSA_SIG.
 */
inline std::optional<std::vector<uint8_t>> ecdsa_raw_to_der(std::string_view raw, size_t expected_raw_len) {
    if (raw.size() != expected_raw_len) return std::nullopt;
    size_t half_len = expected_raw_len / 2;

    BIGNUM* r = BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()), static_cast<int>(half_len), nullptr);
    BIGNUM* s = BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data() + half_len), static_cast<int>(half_len), nullptr);
    if (!r || !s) {
        if (r) BN_free(r);
        if (s) BN_free(s);
        return std::nullopt;
    }

    ECDSA_SIG* sig = ECDSA_SIG_new();
    if (!sig) {
        BN_free(r);
        BN_free(s);
        return std::nullopt;
    }
    ECDSA_SIG_set0(sig, r, s); // takes ownership of r and s

    unsigned char* der = nullptr;
    int der_len = i2d_ECDSA_SIG(sig, &der);
    ECDSA_SIG_free(sig);

    if (der_len <= 0 || !der) return std::nullopt;

    std::vector<uint8_t> result(der, der + der_len);
    OPENSSL_free(der);
    return result;
}

} // namespace detail

/**
 * @brief Loads an EVP_PKEY private key from a PEM formatted string.
 */
[[nodiscard]] inline std::shared_ptr<EVP_PKEY> load_private_key_pem(std::string_view pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return nullptr;

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return nullptr;

    return std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free);
}

/**
 * @brief Loads an EVP_PKEY public key from a PEM formatted string (SubjectPublicKeyInfo).
 */
[[nodiscard]] inline std::shared_ptr<EVP_PKEY> load_public_key_pem(std::string_view pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return nullptr;

    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return nullptr;

    return std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free);
}

/**
 * @brief Loads a private key from a PEM file path.
 */
[[nodiscard]] inline std::shared_ptr<EVP_PKEY> load_private_key_file(const std::string& path) {
    return load_private_key_pem(detail::read_file_to_string(path));
}

/**
 * @brief Loads a public key from a PEM file path.
 */
[[nodiscard]] inline std::shared_ptr<EVP_PKEY> load_public_key_file(const std::string& path) {
    return load_public_key_pem(detail::read_file_to_string(path));
}

/**
 * @brief Signs data with HMAC using the specified algorithm.
 */
[[nodiscard]] inline std::optional<std::string> sign_hmac(Algorithm alg, std::string_view key, std::string_view data) {
    const EVP_MD* md = detail::get_evp_md(alg);
    if (!md) return std::nullopt;

    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = 0;
    if (!HMAC(md, key.data(), static_cast<int>(key.size()),
              reinterpret_cast<const unsigned char*>(data.data()), data.size(),
              mac, &mac_len)) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<char*>(mac), mac_len);
}

/**
 * @brief Verifies an HMAC signature using constant-time comparison (mitigating timing attacks).
 */
[[nodiscard]] inline bool verify_hmac(Algorithm alg, std::string_view key, std::string_view data, std::string_view raw_sig) {
    auto expected = sign_hmac(alg, key, data);
    if (!expected || expected->size() != raw_sig.size()) {
        return false;
    }
    return CRYPTO_memcmp(expected->data(), raw_sig.data(), raw_sig.size()) == 0;
}

/**
 * @brief Signs data using an asymmetric private key (RSA, RSA-PSS, or ECDSA).
 */
[[nodiscard]] inline std::optional<std::string> sign_asymmetric(Algorithm alg, EVP_PKEY* pkey, std::string_view data) {
    if (!pkey) return std::nullopt;
    const EVP_MD* md = detail::get_evp_md(alg);
    if (!md) return std::nullopt;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return std::nullopt;

    EVP_PKEY_CTX* pctx = nullptr;
    if (EVP_DigestSignInit(ctx, &pctx, md, nullptr, pkey) <= 0) {
        EVP_MD_CTX_free(ctx);
        return std::nullopt;
    }

    if (is_rsa_pss(alg)) {
        if (EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) <= 0 ||
            EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST) <= 0) {
            EVP_MD_CTX_free(ctx);
            return std::nullopt;
        }
    }

    if (EVP_DigestSignUpdate(ctx, data.data(), data.size()) <= 0) {
        EVP_MD_CTX_free(ctx);
        return std::nullopt;
    }

    size_t siglen = 0;
    if (EVP_DigestSignFinal(ctx, nullptr, &siglen) <= 0) {
        EVP_MD_CTX_free(ctx);
        return std::nullopt;
    }

    std::vector<uint8_t> sig(siglen);
    if (EVP_DigestSignFinal(ctx, sig.data(), &siglen) <= 0) {
        EVP_MD_CTX_free(ctx);
        return std::nullopt;
    }
    EVP_MD_CTX_free(ctx);
    sig.resize(siglen);

    if (is_ecdsa(alg)) {
        size_t expected_len = detail::get_ecdsa_raw_sig_length(alg);
        return detail::ecdsa_der_to_raw(sig.data(), sig.size(), expected_len);
    }

    return std::string(reinterpret_cast<char*>(sig.data()), sig.size());
}

/**
 * @brief Verifies an asymmetric signature against public key (RSA, RSA-PSS, or ECDSA).
 */
[[nodiscard]] inline bool verify_asymmetric(Algorithm alg, EVP_PKEY* pkey, std::string_view data, std::string_view raw_sig) {
    if (!pkey) return false;
    const EVP_MD* md = detail::get_evp_md(alg);
    if (!md) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;

    EVP_PKEY_CTX* pctx = nullptr;
    if (EVP_DigestVerifyInit(ctx, &pctx, md, nullptr, pkey) <= 0) {
        EVP_MD_CTX_free(ctx);
        return false;
    }

    if (is_rsa_pss(alg)) {
        if (EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) <= 0 ||
            EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST) <= 0) {
            EVP_MD_CTX_free(ctx);
            return false;
        }
    }

    if (EVP_DigestVerifyUpdate(ctx, data.data(), data.size()) <= 0) {
        EVP_MD_CTX_free(ctx);
        return false;
    }

    int ok = 0;
    if (is_ecdsa(alg)) {
        size_t expected_raw_len = detail::get_ecdsa_raw_sig_length(alg);
        auto der_opt = detail::ecdsa_raw_to_der(raw_sig, expected_raw_len);
        if (!der_opt) {
            EVP_MD_CTX_free(ctx);
            return false;
        }
        ok = EVP_DigestVerifyFinal(ctx, der_opt->data(), der_opt->size());
    } else {
        ok = EVP_DigestVerifyFinal(ctx, reinterpret_cast<const unsigned char*>(raw_sig.data()), raw_sig.size());
    }

    EVP_MD_CTX_free(ctx);
    return ok == 1;
}

} // namespace aegon::http::jwt
