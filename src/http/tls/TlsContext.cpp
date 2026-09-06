#include "http/tls/TlsContext.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <iostream>

namespace aegon::http::tls {

namespace {

// ALPN wire format: [len][proto][len][proto] -> \x02h3\x02h2\x08http/1.1
static const unsigned char SERVER_ALPN[] = "\x02h3\x02h2\x08http/1.1";
static const unsigned int SERVER_ALPN_LEN = sizeof(SERVER_ALPN) - 1;

int alpn_select_cb(SSL*, const unsigned char** out, unsigned char* outlen,
                   const unsigned char* in, unsigned int inlen, void*) {
    if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen,
                              SERVER_ALPN, SERVER_ALPN_LEN, in, inlen) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_NOACK;
    }
    return SSL_TLSEXT_ERR_OK;
}

} // anonymous namespace

TlsContext::TlsContext() {
    const SSL_METHOD* method = TLS_server_method();
    ctx_ = SSL_CTX_new(method);
    if (ctx_) {
        // Enforce modern TLS 1.2 minimum
        SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
        // Enable standard secure options
        SSL_CTX_set_options(ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
                                  SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_SINGLE_DH_USE | SSL_OP_SINGLE_ECDH_USE);
        setup_alpn();
    }
}

TlsContext::~TlsContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

TlsContext::TlsContext(TlsContext&& other) noexcept : ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

TlsContext& TlsContext::operator=(TlsContext&& other) noexcept {
    if (this != &other) {
        if (ctx_) SSL_CTX_free(ctx_);
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

void TlsContext::setup_alpn() {
    if (ctx_) {
        SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, nullptr);
    }
}

bool TlsContext::load_cert_and_key(const std::string& cert_path, const std::string& key_path) {
    if (!ctx_) return false;

    if (SSL_CTX_use_certificate_chain_file(ctx_, cert_path.c_str()) <= 0) {
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx_, key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        return false;
    }
    return SSL_CTX_check_private_key(ctx_) == 1;
}

bool TlsContext::generate_self_signed(const std::string& common_name) {
    if (!ctx_) return false;

    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    if (!pkey) return false;

    X509* x509 = X509_new();
    if (!x509) {
        EVP_PKEY_free(pkey);
        return false;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // 1 year

    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (const unsigned char*)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (const unsigned char*)"Aegon", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)common_name.c_str(), -1, -1, 0);
    X509_set_issuer_name(x509, name);

    if (X509_sign(x509, pkey, EVP_sha256()) <= 0) {
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }

    bool ok = (SSL_CTX_use_certificate(ctx_, x509) == 1) &&
              (SSL_CTX_use_PrivateKey(ctx_, pkey) == 1) &&
              (SSL_CTX_check_private_key(ctx_) == 1);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return ok;
}

} // namespace aegon::http::tls
