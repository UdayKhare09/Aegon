#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>

#include "http/jwt/JwtAlgorithm.h"
#include "http/jwt/JwtSigner.h"
#include "http/jwt/JwtVerifier.h"
#include "http/jwt/JwtParser.h"
#include "http/jwt/JwtDenylist.h"

using namespace aegon::http::jwt;

struct UserClaims {
    uint64_t user_id{0};
    std::string email{};
    std::vector<std::string> roles{};
};

struct CustomClaims {
    std::string iss;
    std::string aud;
    std::string sub;
};

// RSA Keypair generator
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

// EC Keypair generator
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

void test_hmac_algorithms() {
    std::cout << "[TEST] HMAC algorithms (HS256, HS384, HS512)..." << std::endl;
    std::string secret = "hmac-super-secret-key-that-is-at-least-32-chars-long";

    const Algorithm algs[] = {Algorithm::HS256, Algorithm::HS384, Algorithm::HS512};
    for (auto alg : algs) {
        JwtSigner signer(alg, secret);
        UserClaims claims{42, "admin@example.com", {"admin", "editor"}};
        std::string token = signer.sign(claims, std::chrono::hours(2));

        // 1. Raw parser check
        auto raw_opt = JwtParser::decode_payload<UserClaims>(token);
        assert(raw_opt.has_value());
        assert(raw_opt->user_id == 42);
        assert(raw_opt->email == "admin@example.com");
        assert(raw_opt->roles.size() == 2);

        // 2. Algorithm extraction
        auto header_alg = JwtParser::get_algorithm(token);
        assert(header_alg.has_value() && *header_alg == alg);

        // 3. Verifier check
        JwtVerifier<UserClaims> verifier(alg, secret);
        auto res = verifier.verify(token);
        assert(res.has_value());
        assert(res->claims.user_id == 42);
        assert(res->claims.email == "admin@example.com");
        assert(res->jti.has_value());
        assert(res->exp.has_value());
        assert(res->iat.has_value());

        // 4. Tamper check
        std::string tampered = token;
        tampered[tampered.size() - 3] = (tampered[tampered.size() - 3] == 'X') ? 'Y' : 'X';
        auto bad = verifier.verify(tampered);
        assert(!bad.has_value());
        assert(bad.error() == JwtError::SignatureMismatch);

        // 5. Wrong key check
        JwtVerifier<UserClaims> wrong_verifier(alg, "different-secret-key-12345");
        auto bad_key = wrong_verifier.verify(token);
        assert(!bad_key.has_value());
        assert(bad_key.error() == JwtError::SignatureMismatch);
    }
    std::cout << "  -> PASS\n";
}

void test_rsa_algorithms() {
    std::cout << "[TEST] RSA algorithms (RS256, RS384, RS512)..." << std::endl;
    std::string priv_pem, pub_pem;
    generate_rsa_keys(priv_pem, pub_pem, 2048);

    const Algorithm algs[] = {Algorithm::RS256, Algorithm::RS384, Algorithm::RS512};
    for (auto alg : algs) {
        JwtSigner signer(alg, priv_pem);
        UserClaims claims{100, "user@rsa.com", {"viewer"}};
        std::string token = signer.sign(claims, std::chrono::hours(1));

        JwtVerifier<UserClaims> verifier(alg, pub_pem);
        auto res = verifier.verify(token);
        assert(res.has_value());
        assert(res->claims.user_id == 100);
        assert(res->claims.email == "user@rsa.com");

        // Algorithm mismatch test
        Algorithm other_alg = (alg == Algorithm::RS256) ? Algorithm::RS512 : Algorithm::RS256;
        JwtVerifier<UserClaims> wrong_alg_verifier(other_alg, pub_pem);
        auto bad_alg = wrong_alg_verifier.verify(token);
        assert(!bad_alg.has_value());
        assert(bad_alg.error() == JwtError::AlgorithmMismatch);
    }
    std::cout << "  -> PASS\n";
}

void test_rsa_pss_algorithms() {
    std::cout << "[TEST] RSA-PSS algorithms (PS256, PS384, PS512)..." << std::endl;
    std::string priv_pem, pub_pem;
    generate_rsa_keys(priv_pem, pub_pem, 2048);

    const Algorithm algs[] = {Algorithm::PS256, Algorithm::PS384, Algorithm::PS512};
    for (auto alg : algs) {
        JwtSigner signer(alg, priv_pem);
        UserClaims claims{200, "user@pss.com", {"signer"}};
        std::string token = signer.sign(claims, std::chrono::hours(1));

        JwtVerifier<UserClaims> verifier(alg, pub_pem);
        auto res = verifier.verify(token);
        assert(res.has_value());
        assert(res->claims.user_id == 200);
        assert(res->claims.roles[0] == "signer");
    }
    std::cout << "  -> PASS\n";
}

void test_ecdsa_algorithms() {
    std::cout << "[TEST] ECDSA algorithms (ES256, ES384, ES512)..." << std::endl;

    struct EcTestCase {
        Algorithm alg;
        const char* curve;
    };
    const EcTestCase cases[] = {
        {Algorithm::ES256, "P-256"},
        {Algorithm::ES384, "P-384"},
        {Algorithm::ES512, "P-521"}
    };

    for (const auto& c : cases) {
        std::string priv_pem, pub_pem;
        generate_ec_keys(c.curve, priv_pem, pub_pem);

        JwtSigner signer(c.alg, priv_pem);
        UserClaims claims{300, "user@ecdsa.com", {"ec-role"}};
        std::string token = signer.sign(claims, std::chrono::hours(1));

        JwtVerifier<UserClaims> verifier(c.alg, pub_pem);
        auto res = verifier.verify(token);
        assert(res.has_value());
        assert(res->claims.user_id == 300);
        assert(res->claims.roles[0] == "ec-role");

        // Tamper test
        std::string tampered = token;
        tampered[tampered.size() - 2] = (tampered[tampered.size() - 2] == 'A') ? 'B' : 'A';
        auto bad = verifier.verify(tampered);
        assert(!bad.has_value());
        assert(bad.error() == JwtError::SignatureMismatch);
    }
    std::cout << "  -> PASS\n";
}

void test_claims_validation() {
    std::cout << "[TEST] Standard claims validation (exp, iat, nbf, iss, aud, jti)..." << std::endl;
    std::string secret = "claim-validation-secret-key-32-chars";
    JwtSigner signer(Algorithm::HS256, secret);

    // 1. Expired Token
    {
        // Negative TTL produces an expired token
        std::string token = signer.sign(CustomClaims{"aegon", "api", "u1"}, std::chrono::seconds(-10));
        JwtVerifier<CustomClaims> verifier(Algorithm::HS256, secret);
        auto res = verifier.verify(token);
        assert(!res.has_value());
        assert(res.error() == JwtError::TokenExpired);

        // Clock skew leeway absorbs expiration
        JwtVerifier<CustomClaims> leeway_verifier(Algorithm::HS256, secret, {
            .leeway_seconds = 30
        });
        auto leeway_res = leeway_verifier.verify(token);
        assert(leeway_res.has_value());
    }

    // 2. Not Before (nbf)
    {
        auto future_time = std::chrono::duration_cast<std::chrono::seconds>(
            (std::chrono::system_clock::now() + std::chrono::hours(1)).time_since_epoch()
        ).count();

        std::string token = signer.sign(CustomClaims{"aegon", "api", "u2"}, std::chrono::hours(2), future_time);
        JwtVerifier<CustomClaims> verifier(Algorithm::HS256, secret);
        auto res = verifier.verify(token);
        assert(!res.has_value());
        assert(res.error() == JwtError::TokenNotYetValid);
    }

    // 3. Issuer mismatch
    {
        std::string token = signer.sign(CustomClaims{"wrong-issuer", "api", "u3"});
        JwtVerifier<CustomClaims> verifier(Algorithm::HS256, secret, {
            .issuer = "expected-issuer"
        });
        auto res = verifier.verify(token);
        assert(!res.has_value());
        assert(res.error() == JwtError::IssuerMismatch);

        // Matching issuer passes
        JwtVerifier<CustomClaims> match_verifier(Algorithm::HS256, secret, {
            .issuer = "wrong-issuer"
        });
        assert(match_verifier.verify(token).has_value());
    }

    // 4. Audience mismatch
    {
        std::string token = signer.sign(CustomClaims{"aegon", "web", "u4"});
        JwtVerifier<CustomClaims> verifier(Algorithm::HS256, secret, {
            .audience = "mobile-app"
        });
        auto res = verifier.verify(token);
        assert(!res.has_value());
        assert(res.error() == JwtError::AudienceMismatch);

        // Matching audience passes
        JwtVerifier<CustomClaims> match_verifier(Algorithm::HS256, secret, {
            .audience = "web"
        });
        assert(match_verifier.verify(token).has_value());
    }

    // 5. JTI and Replay Denylist
    {
        std::string token = signer.sign(CustomClaims{"aegon", "api", "u5"});
        auto denylist = std::make_shared<InMemoryJwtDenylist>();
        JwtVerifier<CustomClaims> verifier(Algorithm::HS256, secret, {
            .denylist = denylist
        });

        auto res = verifier.verify(token);
        assert(res.has_value());
        assert(res->jti.has_value());

        // Revoke token
        denylist->revoke(*res->jti, std::chrono::system_clock::now() + std::chrono::hours(1));
        assert(denylist->is_revoked(*res->jti));
        assert(denylist->size() == 1);

        auto revoked_res = verifier.verify(token);
        assert(!revoked_res.has_value());
        assert(revoked_res.error() == JwtError::RevokedToken);
    }

    std::cout << "  -> PASS\n";
}

void test_error_paths() {
    std::cout << "[TEST] Malformed tokens and error paths..." << std::endl;
    std::string secret = "test-secret-key-32-chars-long!";
    JwtVerifier<UserClaims> verifier(Algorithm::HS256, secret);

    // Empty token
    assert(verifier.verify("").error() == JwtError::MalformedToken);

    // Only one dot
    assert(verifier.verify("abc.def").error() == JwtError::MalformedToken);

    // Four dots
    assert(verifier.verify("a.b.c.d").error() == JwtError::MalformedToken);

    // Invalid base64 in header
    assert(verifier.verify("invalid#b64#.payload.sig").error() == JwtError::MalformedToken);

    std::cout << "  -> PASS\n";
}

void test_key_from_file() {
    std::cout << "[TEST] Key loading from file (from_file)..." << std::endl;
    std::string priv_pem, pub_pem;
    generate_rsa_keys(priv_pem, pub_pem, 2048);

    namespace fs = std::filesystem;
    fs::path temp_dir = fs::temp_directory_path() / "aegon_jwt_test";
    fs::create_directories(temp_dir);

    fs::path priv_file = temp_dir / "private.pem";
    fs::path pub_file = temp_dir / "public.pem";

    {
        std::ofstream f(priv_file);
        f << priv_pem;
    }
    {
        std::ofstream f(pub_file);
        f << pub_pem;
    }

    auto signer = JwtSigner<UserClaims>::from_file(Algorithm::RS256, priv_file.string());
    std::string token = signer.sign(UserClaims{999, "file@test.com", {"file-role"}});

    auto verifier = JwtVerifier<UserClaims>::from_file(Algorithm::RS256, pub_file.string());
    auto res = verifier.verify(token);
    assert(res.has_value());
    assert(res->claims.user_id == 999);
    assert(res->claims.email == "file@test.com");

    fs::remove_all(temp_dir);
    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "    Aegon JWT Engine Comprehensive Tests\n";
    std::cout << "========================================\n";

    test_hmac_algorithms();
    test_rsa_algorithms();
    test_rsa_pss_algorithms();
    test_ecdsa_algorithms();
    test_claims_validation();
    test_error_paths();
    test_key_from_file();

    std::cout << "\nALL JWT TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
