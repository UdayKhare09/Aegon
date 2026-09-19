# JSON Web Tokens (JWT)

Aegon provides a header-only, high-performance JWT engine backed by OpenSSL (already linked for TLS). It offers zero-dependency, compile-time type safety via Glaze JSON serialization, comprehensive algorithm support, and automatic claim management.

```cpp
#include "http/jwt/JwtAlgorithm.h"
#include "http/jwt/JwtSigner.h"
#include "http/jwt/JwtVerifier.h"
#include "http/jwt/JwtParser.h"
#include "http/jwt/JwtDenylist.h"

using namespace aegon::http::jwt;
```

---

## Supported Cryptographic Algorithms

All algorithms conform strictly to RFC 7515 (JWS) and RFC 7518 (JWA):

| Algorithm | Type | Description | Key Format |
| :--- | :--- | :--- | :--- |
| `Algorithm::HS256` | Symmetric | HMAC using SHA-256 | Shared secret `std::string` |
| `Algorithm::HS384` | Symmetric | HMAC using SHA-384 | Shared secret `std::string` |
| `Algorithm::HS512` | Symmetric | HMAC using SHA-512 | Shared secret `std::string` |
| `Algorithm::RS256` | Asymmetric | RSASSA-PKCS1-v1_5 using SHA-256 | PEM private / public key |
| `Algorithm::RS384` | Asymmetric | RSASSA-PKCS1-v1_5 using SHA-384 | PEM private / public key |
| `Algorithm::RS512` | Asymmetric | RSASSA-PKCS1-v1_5 using SHA-512 | PEM private / public key |
| `Algorithm::PS256` | Asymmetric | RSASSA-PSS using SHA-256 | PEM private / public key |
| `Algorithm::PS384` | Asymmetric | RSASSA-PSS using SHA-384 | PEM private / public key |
| `Algorithm::PS512` | Asymmetric | RSASSA-PSS using SHA-512 | PEM private / public key |
| `Algorithm::ES256` | Asymmetric | ECDSA using P-256 curve and SHA-256 | PEM private / public key |
| `Algorithm::ES384` | Asymmetric | ECDSA using P-384 curve and SHA-384 | PEM private / public key |
| `Algorithm::ES512` | Asymmetric | ECDSA using P-521 curve and SHA-512 | PEM private / public key |

> [!NOTE]
> For ECDSA algorithms (`ES256`, `ES384`, `ES512`), Aegon automatically converts between OpenSSL's ASN.1 DER sequence and the standard raw `R || S` byte concatenation required by RFC 7515 §3.4.

---

## Defining Custom Claims

Claims are defined as standard C++ structs and serialized via **Glaze**:

```cpp
struct UserClaims {
    uint64_t user_id;
    std::string email;
    std::vector<std::string> roles;
    std::string tenant_id{"default"};
};
```

---

## Signing Tokens (`JwtSigner`)

The `JwtSigner` serializes your claims, automatically injects standard claims, computes the signature, and returns the compact JWT string (`header.payload.signature`).

### Auto-Injected Claims

Unless explicitly populated in your custom claims struct, the signer automatically populates:
- `iat` (Issued At) — Current Unix timestamp.
- `exp` (Expiration) — Current timestamp + configured TTL (defaults to 24 hours).
- `nbf` (Not Before) — Current timestamp or user-supplied offset.
- `jti` (JWT ID) — Cryptographically strong RFC 4122 UUID v4 (generated via `aegon::data::UUIDGenerator`).

### Symmetric Signing (HMAC)

```cpp
JwtSigner signer(Algorithm::HS256, "my-super-secret-key-32-chars-long");

UserClaims claims{
    .user_id = 42,
    .email = "admin@example.com",
    .roles = {"admin", "editor"}
};

// Signs with a 24-hour expiration
std::string token = signer.sign(claims, std::chrono::hours(24));
```

### Asymmetric Signing (RSA / ECDSA)

Keys can be provided inline as PEM strings or loaded directly from files:

```cpp
// 1. From inline PEM string
JwtSigner rsa_signer(Algorithm::RS256, private_key_pem);

// 2. Directly from file
auto es_signer = JwtSigner<UserClaims>::from_file(Algorithm::ES256, "/etc/certs/ec_private.pem");
std::string token = es_signer.sign(claims, std::chrono::hours(2));
```

---

## Verifying Tokens (`JwtVerifier`)

`JwtVerifier` performs comprehensive token validation:
1. Validates compact token structure.
2. Decodes header and verifies that the declared algorithm matches the expected configuration.
3. Cryptographically verifies the signature (using constant-time comparison for HMAC to prevent timing attacks).
4. Validates standard temporal claims (`exp`, `nbf`) with optional clock-skew leeway.
5. Validates `iss` (Issuer) and `aud` (Audience) if configured.
6. Checks the `jti` against a revocation denylist if configured.
7. Deserializes the payload into your typed claims struct.

```cpp
JwtVerifier<UserClaims> verifier(Algorithm::HS256, "my-super-secret-key-32-chars-long", {
    .issuer = "aegon-auth",
    .audience = "aegon-api",
    .leeway_seconds = 10 // Clock skew tolerance
});

auto result = verifier.verify(token);

if (result.has_value()) {
    const UserClaims& claims = result->claims;
    std::cout << "Authenticated user: " << claims.email << "\n";
    std::cout << "Token JTI: " << result->jti.value_or("none") << "\n";
} else {
    // result.error() contains the specific JwtError code
    std::cerr << "Verification failed: " << to_string(result.error()) << "\n";
}
```

### Verification Error Codes (`JwtError`)

`JwtVerifier::verify` returns `std::expected<JwtResult<T>, JwtError>`:

| Error Code | Description |
| :--- | :--- |
| `JwtError::MalformedToken` | Structure is malformed, missing dots, or contains invalid Base64URL. |
| `JwtError::AlgorithmMismatch` | Header `alg` does not match the verifier's expected algorithm. |
| `JwtError::SignatureMismatch` | Cryptographic signature check failed (token was tampered with or key is invalid). |
| `JwtError::TokenExpired` | Current time is past `exp` timestamp (beyond configured leeway). |
| `JwtError::TokenNotYetValid` | Current time is before `nbf` timestamp (beyond configured leeway). |
| `JwtError::IssuerMismatch` | Token's `iss` claim does not match configured expected issuer. |
| `JwtError::AudienceMismatch` | Token's `aud` claim does not match configured expected audience. |
| `JwtError::ParseError` | Payload could not be parsed into the destination claims struct. |
| `JwtError::RevokedToken` | Token's `jti` is present in the active denylist. |

---

## Token Revocation & Replay Prevention (`JwtDenylist`)

To support logout revocation or one-time token replay prevention, Aegon provides the `IJwtDenylist` interface and a thread-safe `InMemoryJwtDenylist`:

```cpp
auto denylist = std::make_shared<InMemoryJwtDenylist>();

JwtVerifier<UserClaims> verifier(Algorithm::HS256, secret, {
    .denylist = denylist
});

// Revoke a specific token upon logout
denylist->revoke(token_jti, expires_at);

// Subsequent verification of the token will fail with JwtError::RevokedToken
auto res = verifier.verify(token);
assert(res.error() == JwtError::RevokedToken);
```

For distributed deployments, implement `IJwtDenylist` backed by Redis or an SQL database:

```cpp
class RedisJwtDenylist : public IJwtDenylist {
    RedisClient& redis_;
public:
    explicit RedisJwtDenylist(RedisClient& r) : redis_(r) {}

    bool is_revoked(std::string_view jti) const override {
        // e.g. redis.exists("revoked:" + std::string(jti))
    }

    void revoke(std::string_view jti, std::chrono::system_clock::time_point exp) override {
        // e.g. redis.setex("revoked:" + std::string(jti), ttl, "1")
    }
};
```

---

## Raw Decoding (`JwtParser`)

When verification has already taken place upstream (e.g. at an API Gateway), or for debugging and logging, `JwtParser` extracts headers and payloads without cryptographic verification:

```cpp
// Decode raw header JSON
std::optional<std::string> header = JwtParser::decode_header(token);

// Extract declared algorithm
std::optional<Algorithm> alg = JwtParser::get_algorithm(token);

// Extract Key ID (kid) from header
std::optional<std::string> kid = JwtParser::get_kid(token);

// Decode payload into typed struct without checking signature
std::optional<UserClaims> claims = JwtParser::decode_payload<UserClaims>(token);
```

---

## JSON Web Key Sets (JWKS) & Key Rotation

Aegon provides built-in support for **RFC 7517 JSON Web Key Sets (JWKS)** for both roles: acting as an **Identity Provider** (issuing tokens and publishing `/.well-known/jwks.json`) and acting as a **Resource Server** (consuming and verifying tokens from Google, Auth0, Keycloak, or another Aegon service).

```cpp
#include "http/jwt/Jwks.h"
```

### 1. Aegon as Identity Provider (Serving JWKS)

To publish your active public keys so clients and API gateways can verify your tokens:

```cpp
// 1. Build a JWKS with active public keys
Jwks jwks;
jwks.add_key("auth-2026-v1", Algorithm::RS256, rsa_public_pem_v1);
jwks.add_key("auth-2026-v2", Algorithm::RS256, rsa_public_pem_v2); // New rotation key

// 2. Expose standard JWKS endpoint
app.router().get("/.well-known/jwks.json", [&jwks](Context& ctx) {
    ctx.res().header("Content-Type", "application/json")
             .body(jwks.to_json());
});

// 3. Sign tokens stamped with the active Key ID (kid)
JwtSigner signer(Algorithm::RS256, rsa_private_pem_v2, "auth-2026-v2");
std::string token = signer.sign(UserClaims{ .user_id = 42 });
```

The resulting token header will automatically be:
```json
{
  "alg": "RS256",
  "typ": "JWT",
  "kid": "auth-2026-v2"
}
```

### 2. Aegon as Consumer (Verifying via JWKS)

When verifying incoming tokens against a JWKS (either fetched from an external provider or loaded locally):

```cpp
// Parse JWKS document
std::string jwks_json = co_await http_client.get("https://auth.example.com/.well-known/jwks.json");
auto jwks = std::make_shared<Jwks>(Jwks::from_json(jwks_json));

// Initialize verifier with JWKS (no static key needed)
JwtVerifier<UserClaims> verifier(Algorithm::RS256, {
    .issuer = "https://auth.example.com/",
    .audience = "https://api.mycompany.com",
    .jwks = jwks
});

// Verifies tokens signed with ANY valid key in the JWKS:
auto result = verifier.verify(token);
if (result.has_value()) {
    std::cout << "Verified using key: " << result->kid.value_or("none") << "\n";
}
```

### 3. Dynamic Key Rotation (`key_resolver`)

For applications that resolve keys on-demand from a cache or database:

```cpp
JwtVerifier<UserClaims> verifier(Algorithm::RS256, {
    .key_resolver = [&key_cache](std::string_view kid) -> std::optional<std::string> {
        // Look up public PEM for this kid, or fetch from network if missing
        return key_cache.find_key(kid);
    }
});
```

### 4. Zero-Downtime Key Rotation in Production

During a key rotation cycle:
1. Generate the new key pair (`v2`).
2. Add the `v2` public key to your `Jwks` alongside `v1`.
3. Update `JwtSigner` to start signing with `v2` and `"kid": "v2"`.
4. API verifiers holding the updated `Jwks` can verify both `v1` (unexpired existing tokens) and `v2` (newly issued tokens) simultaneously.
5. After all `v1` tokens have expired (e.g., 24 hours), remove `v1` from the `Jwks`. **Zero downtime and zero logged-out users.**

