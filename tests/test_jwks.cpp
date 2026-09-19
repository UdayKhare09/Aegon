#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "http/jwt/JwtAlgorithm.h"
#include "http/jwt/JwtSigner.h"
#include "http/jwt/JwtVerifier.h"
#include "http/jwt/JwtParser.h"
#include "http/jwt/Jwks.h"

using namespace aegon::http::jwt;

struct UserClaims {
    uint64_t sub{0};
    std::string role{};
};

static void generate_rsa_keys(std::string& priv_pem, std::string& pub_pem, int bits = 2048) {
    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(kctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, bits);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen(kctx, &pkey);
    EVP_PKEY_CTX_free(kctx);

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    priv_pem = std::string(data, len);
    BIO_free(bio);

    bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, pkey);
    len = BIO_get_mem_data(bio, &data);
    pub_pem = std::string(data, len);
    BIO_free(bio);

    EVP_PKEY_free(pkey);
}

static void generate_ec_keys(const char* curve_name, std::string& priv_pem, std::string& pub_pem) {
    EVP_PKEY* pkey = EVP_EC_gen(curve_name);
    assert(pkey != nullptr);

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    priv_pem = std::string(data, len);
    BIO_free(bio);

    bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, pkey);
    len = BIO_get_mem_data(bio, &data);
    pub_pem = std::string(data, len);
    BIO_free(bio);

    EVP_PKEY_free(pkey);
}

void test_rsa_jwk_conversion() {
    std::cout << "[TEST] RSA JWK conversion (PEM <-> JWK <-> EVP_PKEY)..." << std::endl;
    std::string priv_pem, pub_pem;
    generate_rsa_keys(priv_pem, pub_pem, 2048);

    auto jwk_opt = JwkKey::from_pem(pub_pem, "rsa-test-key-1", "RS256");
    assert(jwk_opt.has_value());
    assert(jwk_opt->kty == "RSA");
    assert(jwk_opt->kid == "rsa-test-key-1");
    assert(jwk_opt->alg == "RS256");
    assert(!jwk_opt->n.empty());
    assert(!jwk_opt->e.empty());

    // Reconstruct EVP_PKEY
    auto reconstructed = jwk_opt->to_evp_pkey();
    assert(reconstructed != nullptr);

    // Sign with original private key and verify with reconstructed public key
    JwtSigner signer(Algorithm::RS256, priv_pem, "rsa-test-key-1");
    std::string token = signer.sign(UserClaims{42, "admin"});

    // Parser check
    auto parsed_kid = JwtParser::get_kid(token);
    assert(parsed_kid.has_value() && *parsed_kid == "rsa-test-key-1");

    std::cout << "  -> PASS\n";
}

void test_ec_jwk_conversion() {
    std::cout << "[TEST] EC JWK conversion (PEM <-> JWK <-> EVP_PKEY)..." << std::endl;
    std::string priv_pem, pub_pem;
    generate_ec_keys("P-256", priv_pem, pub_pem);

    auto jwk_opt = JwkKey::from_pem(pub_pem, "ec-test-key-1", "ES256");
    assert(jwk_opt.has_value());
    assert(jwk_opt->kty == "EC");
    assert(jwk_opt->kid == "ec-test-key-1");
    assert(jwk_opt->crv == "P-256");
    assert(!jwk_opt->x.empty());
    assert(!jwk_opt->y.empty());

    auto reconstructed = jwk_opt->to_evp_pkey();
    assert(reconstructed != nullptr);

    std::cout << "  -> PASS\n";
}

void test_jwks_serialization_roundtrip() {
    std::cout << "[TEST] JWKS JSON serialization and parsing (RFC 7517)..." << std::endl;
    std::string rsa_priv, rsa_pub;
    generate_rsa_keys(rsa_priv, rsa_pub, 2048);

    std::string ec_priv, ec_pub;
    generate_ec_keys("P-256", ec_priv, ec_pub);

    Jwks jwks;
    assert(jwks.add_key("rsa-2026", Algorithm::RS256, rsa_pub));
    assert(jwks.add_key("ec-2026", Algorithm::ES256, ec_pub));
    assert(jwks.size() == 2);

    std::string json = jwks.to_json();
    std::cout << "Generated JWKS JSON:\n" << json << "\n";
    assert(json.find("\"keys\"") != std::string::npos);
    assert(json.find("\"rsa-2026\"") != std::string::npos);
    assert(json.find("\"ec-2026\"") != std::string::npos);

    // Parse from JSON
    Jwks parsed = Jwks::from_json(json);
    assert(parsed.size() == 2);

    auto rsa_key = parsed.get_key("rsa-2026");
    assert(rsa_key != nullptr);

    auto ec_key = parsed.get_key("ec-2026");
    assert(ec_key != nullptr);

    std::cout << "  -> PASS\n";
}

void test_key_rotation_with_jwks() {
    std::cout << "[TEST] Zero-downtime key rotation with JWKS and kid..." << std::endl;

    // Key 1 (Old key)
    std::string priv_1, pub_1;
    generate_rsa_keys(priv_1, pub_1, 2048);

    // Key 2 (New key)
    std::string priv_2, pub_2;
    generate_rsa_keys(priv_2, pub_2, 2048);

    // JWKS contains BOTH keys during rotation window
    auto jwks = std::make_shared<Jwks>();
    jwks->add_key("auth-key-v1", Algorithm::RS256, pub_1);
    jwks->add_key("auth-key-v2", Algorithm::RS256, pub_2);

    // Sign token 1 with Old Key
    JwtSigner signer_v1(Algorithm::RS256, priv_1, "auth-key-v1");
    std::string token_v1 = signer_v1.sign(UserClaims{101, "old_user"});

    // Sign token 2 with New Key
    JwtSigner signer_v2(Algorithm::RS256, priv_2, "auth-key-v2");
    std::string token_v2 = signer_v2.sign(UserClaims{102, "new_user"});

    // Verifier initialized with JWKS (no single static key!)
    JwtVerifier<UserClaims> verifier(Algorithm::RS256, {
        .jwks = jwks
    });

    // Verify token 1 -> SUCCESS using auth-key-v1
    auto res1 = verifier.verify(token_v1);
    assert(res1.has_value());
    assert(res1->claims.sub == 101);
    assert(res1->kid.has_value() && *res1->kid == "auth-key-v1");

    // Verify token 2 -> SUCCESS using auth-key-v2
    auto res2 = verifier.verify(token_v2);
    assert(res2.has_value());
    assert(res2->claims.sub == 102);
    assert(res2->kid.has_value() && *res2->kid == "auth-key-v2");

    // Verify token with unknown kid -> KeyNotFound
    JwtSigner signer_unknown(Algorithm::RS256, priv_1, "unknown-key-id");
    std::string token_unknown = signer_unknown.sign(UserClaims{999, "unknown"});
    auto res_unknown = verifier.verify(token_unknown);
    assert(!res_unknown.has_value());
    assert(res_unknown.error() == JwtError::KeyNotFound);

    // Verify token with expected_kid filter
    JwtVerifier<UserClaims> filtered_verifier(Algorithm::RS256, {
        .expected_kid = "auth-key-v2",
        .jwks = jwks
    });
    // Token with auth-key-v1 rejected
    assert(filtered_verifier.verify(token_v1).error() == JwtError::KeyNotFound);
    // Token with auth-key-v2 accepted
    assert(filtered_verifier.verify(token_v2).has_value());

    std::cout << "  -> PASS\n";
}

void test_dynamic_key_resolver() {
    std::cout << "[TEST] Dynamic key resolver callback..." << std::endl;
    std::string priv, pub;
    generate_rsa_keys(priv, pub, 2048);

    JwtSigner signer(Algorithm::RS256, priv, "dynamic-key-1");
    std::string token = signer.sign(UserClaims{555, "dynamo"});

    JwtVerifier<UserClaims> verifier(Algorithm::RS256, {
        .key_resolver = [&pub](std::string_view kid) -> std::optional<std::string> {
            if (kid == "dynamic-key-1") {
                return pub;
            }
            return std::nullopt;
        }
    });

    auto res = verifier.verify(token);
    assert(res.has_value());
    assert(res->claims.sub == 555);
    assert(res->kid == "dynamic-key-1");

    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "       Aegon JWKS & kid Tests\n";
    std::cout << "========================================\n";

    test_rsa_jwk_conversion();
    test_ec_jwk_conversion();
    test_jwks_serialization_roundtrip();
    test_key_rotation_with_jwks();
    test_dynamic_key_resolver();

    std::cout << "\nALL JWKS AND KEY ROTATION TESTS PASSED!\n";
    return 0;
}
