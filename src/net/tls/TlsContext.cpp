#include "net/tls/TlsContext.h"
#include "net/tls/TlsStream.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <cstring>
#include <iostream>

namespace aegon::net::tls {

namespace {

// Default ALPN wire format: [len][proto][len][proto] -> \x02h3\x02h2\x08http/1.1
static const unsigned char DEFAULT_SERVER_ALPN[] = "\x02h3\x02h2\x08http/1.1";
static const unsigned int DEFAULT_SERVER_ALPN_LEN = sizeof(DEFAULT_SERVER_ALPN) - 1;

int alpn_select_cb(SSL*, const unsigned char** out, unsigned char* outlen,
                   const unsigned char* in, unsigned int inlen, void* arg) {
    auto* ctx_obj = static_cast<TlsContext*>(arg);
    const unsigned char* server_alpn = DEFAULT_SERVER_ALPN;
    unsigned int server_alpn_len = DEFAULT_SERVER_ALPN_LEN;

    if (ctx_obj && !ctx_obj->alpn_wire().empty()) {
        server_alpn = ctx_obj->alpn_wire().data();
        server_alpn_len = static_cast<unsigned int>(ctx_obj->alpn_wire().size());
    }

    if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen,
                              server_alpn, server_alpn_len, in, inlen) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    return SSL_TLSEXT_ERR_OK;
}

void keylog_callback(const SSL* ssl, const char* line) {
    if (!ssl || !line) return;
    auto* stream = static_cast<TlsStream*>(SSL_get_ex_data(ssl, get_tls_stream_ex_index()));
    if (!stream) return;
    if (std::strncmp(line, "SERVER_TRAFFIC_SECRET_0 ", 24) == 0) {
        const char* secret_hex = std::strrchr(line, ' ');
        if (secret_hex && *(secret_hex + 1)) {
            stream->set_server_traffic_secret(secret_hex + 1);
        }
    }
}

} // anonymous namespace

TlsContext::TlsContext() {
    const SSL_METHOD* method = TLS_server_method();
    ctx_ = SSL_CTX_new(method);
    if (ctx_) {
        // Enforce modern TLS 1.2 minimum
        SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
        // Enable standard secure options + kTLS TX acceleration
        SSL_CTX_set_options(ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
                                  SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_SINGLE_DH_USE | SSL_OP_SINGLE_ECDH_USE |
                                  SSL_OP_NO_TICKET
#if defined(SSL_OP_ENABLE_KTLS)
                                  | SSL_OP_ENABLE_KTLS
#endif
        );
        SSL_CTX_set_num_tickets(ctx_, 0);
        SSL_CTX_set_ciphersuites(ctx_, "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
        SSL_CTX_set_keylog_callback(ctx_, keylog_callback);
        setup_alpn();
    }
}

TlsContext::~TlsContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

TlsContext::TlsContext(TlsContext&& other) noexcept
    : ctx_(other.ctx_), alpn_wire_(std::move(other.alpn_wire_)) {
    other.ctx_ = nullptr;
}

TlsContext& TlsContext::operator=(TlsContext&& other) noexcept {
    if (this != &other) {
        if (ctx_) SSL_CTX_free(ctx_);
        ctx_ = other.ctx_;
        alpn_wire_ = std::move(other.alpn_wire_);
        other.ctx_ = nullptr;
    }
    return *this;
}

void TlsContext::setup_alpn() {
    if (ctx_) {
        SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, this);
        SSL_CTX_set_client_hello_cb(ctx_, [](SSL* s, int* al, void*) -> int {
            if (!SSL_is_quic(s)) {
                return SSL_CLIENT_HELLO_SUCCESS;
            }
            const unsigned char* p = nullptr;
            size_t len = 0;
            // RFC 9000/9001: quic_transport_parameters extension type is 57 (0x39) or 0xffa5 (draft-29)
            int ext_present = SSL_client_hello_get0_ext(s, 57, &p, &len) ||
                              SSL_client_hello_get0_ext(s, 0xffa5, &p, &len);
            if (!ext_present) {
                *al = SSL_AD_MISSING_EXTENSION;
                return SSL_CLIENT_HELLO_ERROR;
            }
            return SSL_CLIENT_HELLO_SUCCESS;
        }, nullptr);
    }
}

void TlsContext::set_alpn_protocols(const std::vector<std::string>& protocols) {
    alpn_wire_.clear();
    for (const auto& proto : protocols) {
        if (!proto.empty() && proto.size() <= 255) {
            alpn_wire_.push_back(static_cast<unsigned char>(proto.size()));
            alpn_wire_.insert(alpn_wire_.end(), proto.begin(), proto.end());
        }
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
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // 1 year validity

    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("US"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("Aegon Dev"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name.c_str()), -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // Add Subject Alternative Name (SAN) extension for localhost / IP compatibility
    X509V3_CTX ctx_v3;
    X509V3_set_ctx_nodb(&ctx_v3);
    X509V3_set_ctx(&ctx_v3, x509, x509, nullptr, nullptr, 0);
    std::string san_str = "DNS:" + common_name + ",DNS:localhost,IP:127.0.0.1,IP:::1";
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx_v3, NID_subject_alt_name, san_str.c_str());
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }

    int rc1 = SSL_CTX_use_certificate(ctx_, x509);
    int rc2 = SSL_CTX_use_PrivateKey(ctx_, pkey);
    int rc3 = SSL_CTX_check_private_key(ctx_);

    X509_free(x509);
    EVP_PKEY_free(pkey);

    return (rc1 == 1 && rc2 == 1 && rc3 == 1);
}

} // namespace aegon::net::tls
