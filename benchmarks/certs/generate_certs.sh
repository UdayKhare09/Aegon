#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "==> Generating benchmark TLS certificates..."

# Generate private key (ECDSA prime256v1 or RSA 2048)
openssl ecparam -genkey -name prime256v1 -out server.key

# Generate self-signed certificate with SAN localhost / 127.0.0.1
openssl req -new -x509 -key server.key -out server.crt -days 365 -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

echo "==> Generated server.key and server.crt in $SCRIPT_DIR"
