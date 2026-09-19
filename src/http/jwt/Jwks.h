#pragma once

#include "http/jwt/JwtAlgorithm.h"
#include <glaze/glaze.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>
#include <algorithm>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <openssl/bn.h>

namespace aegon::http::jwt {

/**
 * @brief Represents a single JSON Web Key (RFC 7517).
 */
struct JwkKey {
    std::string kty;          // "RSA" or "EC"
    std::string use{"sig"};   // "sig"
    std::string alg;          // "RS256", "ES256", etc.
    std::string kid;          // Key ID

    // RSA-specific parameters (Base64URL encoded)
    std::string n;
    std::string e;

    // EC-specific parameters (Base64URL encoded)
    std::string crv;          // "P-256", "P-384", "P-521"
    std::string x;
    std::string y;

    /**
     * @brief Converts this JWK into an OpenSSL EVP_PKEY public key.
     */
    [[nodiscard]] std::shared_ptr<EVP_PKEY> to_evp_pkey() const {
        if (kty == "RSA") {
            auto n_bin = base64url_decode(n);
            auto e_bin = base64url_decode(e);
            if (!n_bin || !e_bin) return nullptr;

            BIGNUM* n_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(n_bin->data()), static_cast<int>(n_bin->size()), nullptr);
            BIGNUM* e_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(e_bin->data()), static_cast<int>(e_bin->size()), nullptr);
            if (!n_bn || !e_bn) {
                if (n_bn) BN_free(n_bn);
                if (e_bn) BN_free(e_bn);
                return nullptr;
            }

            OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
            OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, n_bn);
            OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, e_bn);
            OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
            OSSL_PARAM_BLD_free(bld);

            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
            EVP_PKEY_fromdata_init(ctx);
            EVP_PKEY* pkey = nullptr;
            int ok = EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params);

            EVP_PKEY_CTX_free(ctx);
            OSSL_PARAM_free(params);
            BN_free(n_bn);
            BN_free(e_bn);

            if (ok != 1 || !pkey) return nullptr;
            return std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free);

        } else if (kty == "EC") {
            auto x_bin = base64url_decode(x);
            auto y_bin = base64url_decode(y);
            if (!x_bin || !y_bin) return nullptr;

            BIGNUM* x_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(x_bin->data()), static_cast<int>(x_bin->size()), nullptr);
            BIGNUM* y_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(y_bin->data()), static_cast<int>(y_bin->size()), nullptr);
            if (!x_bn || !y_bn) {
                if (x_bn) BN_free(x_bn);
                if (y_bn) BN_free(y_bn);
                return nullptr;
            }

            OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
            OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, crv.c_str(), 0);
            OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_EC_PUB_X, x_bn);
            OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_EC_PUB_Y, y_bn);
            OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
            OSSL_PARAM_BLD_free(bld);

            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
            EVP_PKEY_fromdata_init(ctx);
            EVP_PKEY* pkey = nullptr;
            int ok = EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params);

            EVP_PKEY_CTX_free(ctx);
            OSSL_PARAM_free(params);
            BN_free(x_bn);
            BN_free(y_bn);

            if (ok != 1 || !pkey) return nullptr;
            return std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free);
        }

        return nullptr;
    }

    /**
     * @brief Constructs a JwkKey by extracting public key parameters from an OpenSSL EVP_PKEY.
     */
    [[nodiscard]] static std::optional<JwkKey> from_evp_pkey(EVP_PKEY* pkey, std::string_view key_id, std::string_view algorithm = "") {
        if (!pkey) return std::nullopt;

        JwkKey jwk;
        jwk.kid = std::string(key_id);
        jwk.use = "sig";
        if (!algorithm.empty()) {
            jwk.alg = std::string(algorithm);
        }

        int base_id = EVP_PKEY_get_base_id(pkey);
        if (base_id == EVP_PKEY_RSA || base_id == EVP_PKEY_RSA_PSS) {
            jwk.kty = "RSA";
            if (jwk.alg.empty()) {
                jwk.alg = (base_id == EVP_PKEY_RSA_PSS) ? "PS256" : "RS256";
            }

            BIGNUM* n_bn = nullptr;
            BIGNUM* e_bn = nullptr;
            if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N, &n_bn) <= 0 ||
                EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_E, &e_bn) <= 0) {
                if (n_bn) BN_free(n_bn);
                if (e_bn) BN_free(e_bn);
                return std::nullopt;
            }

            int n_len = BN_num_bytes(n_bn);
            int e_len = BN_num_bytes(e_bn);
            std::vector<uint8_t> n_buf(n_len);
            std::vector<uint8_t> e_buf(e_len);
            BN_bn2bin(n_bn, n_buf.data());
            BN_bn2bin(e_bn, e_buf.data());
            BN_free(n_bn);
            BN_free(e_bn);

            jwk.n = base64url_encode(n_buf.data(), n_len);
            jwk.e = base64url_encode(e_buf.data(), e_len);
            return jwk;

        } else if (base_id == EVP_PKEY_EC) {
            jwk.kty = "EC";

            char group_name[64] = {0};
            size_t group_len = 0;
            if (EVP_PKEY_get_utf8_string_param(pkey, OSSL_PKEY_PARAM_GROUP_NAME, group_name, sizeof(group_name), &group_len) <= 0) {
                return std::nullopt;
            }

            std::string_view gname(group_name, group_len);
            size_t coord_bytes = 32;
            if (gname == "P-256" || gname == "prime256v1") {
                jwk.crv = "P-256";
                coord_bytes = 32;
                if (jwk.alg.empty()) jwk.alg = "ES256";
            } else if (gname == "P-384" || gname == "secp384r1") {
                jwk.crv = "P-384";
                coord_bytes = 48;
                if (jwk.alg.empty()) jwk.alg = "ES384";
            } else if (gname == "P-521" || gname == "secp521r1") {
                jwk.crv = "P-521";
                coord_bytes = 66;
                if (jwk.alg.empty()) jwk.alg = "ES512";
            } else {
                jwk.crv = std::string(gname);
            }

            BIGNUM* x_bn = nullptr;
            BIGNUM* y_bn = nullptr;
            if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_EC_PUB_X, &x_bn) <= 0 ||
                EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_EC_PUB_Y, &y_bn) <= 0) {
                if (x_bn) BN_free(x_bn);
                if (y_bn) BN_free(y_bn);
                return std::nullopt;
            }

            std::vector<uint8_t> x_buf(coord_bytes, 0);
            std::vector<uint8_t> y_buf(coord_bytes, 0);
            BN_bn2binpad(x_bn, x_buf.data(), static_cast<int>(coord_bytes));
            BN_bn2binpad(y_bn, y_buf.data(), static_cast<int>(coord_bytes));
            BN_free(x_bn);
            BN_free(y_bn);

            jwk.x = base64url_encode(x_buf.data(), coord_bytes);
            jwk.y = base64url_encode(y_buf.data(), coord_bytes);
            return jwk;
        }

        return std::nullopt;
    }

    /**
     * @brief Constructs a JwkKey by parsing a PEM public key string.
     */
    [[nodiscard]] static std::optional<JwkKey> from_pem(std::string_view pem, std::string_view key_id, std::string_view algorithm = "") {
        auto pkey = load_public_key_pem(pem);
        if (!pkey) {
            pkey = load_private_key_pem(pem); // Private key also contains public key params
        }
        if (!pkey) return std::nullopt;
        return from_evp_pkey(pkey.get(), key_id, algorithm);
    }
};

/**
 * @brief Represents a JSON Web Key Set (RFC 7517).
 */
struct Jwks {
    std::vector<JwkKey> keys;

    /**
     * @brief Serializes the key set to standard RFC 7517 JSON format.
     */
    [[nodiscard]] std::string to_json() const {
        std::string out;
        (void)glz::write_json(*this, out);
        return out;
    }

    /**
     * @brief Parses an RFC 7517 JWKS JSON document (from Google, Keycloak, Auth0, etc.).
     */
    [[nodiscard]] static Jwks from_json(std::string_view json) {
        Jwks jwks;
        (void)glz::read<glz::opts{.error_on_unknown_keys = false}>(jwks, json);
        return jwks;
    }

    /**
     * @brief Adds a public key to the set from a PEM string.
     */
    bool add_key(std::string_view kid, Algorithm alg, std::string_view pem_public_key) {
        auto key_opt = JwkKey::from_pem(pem_public_key, kid, algorithm_to_string(alg));
        if (!key_opt) return false;
        keys.push_back(std::move(*key_opt));
        return true;
    }

    /**
     * @brief Finds and reconstructs the OpenSSL public key corresponding to a given Key ID (kid).
     */
    [[nodiscard]] std::shared_ptr<EVP_PKEY> get_key(std::string_view kid) const {
        for (const auto& k : keys) {
            if (k.kid == kid) {
                return k.to_evp_pkey();
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool empty() const noexcept { return keys.empty(); }
    [[nodiscard]] size_t size() const noexcept { return keys.size(); }
};

} // namespace aegon::http::jwt
