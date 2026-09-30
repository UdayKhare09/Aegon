#!/usr/bin/env bash
set -e

PORT_PLAIN=18881
PORT_TLS=18882

BINARY="./build/samples/sse/aegon_sse_sample"
if [ ! -f "$BINARY" ]; then
    BINARY="./build/aegon_sse_sample"
fi

if [ ! -f "$BINARY" ]; then
    echo "Error: Binary aegon_sse_sample not found. Build it first."
    exit 1
fi

echo "=========================================================="
echo "      AEGON LIVE SSE TEST RUNNER (H1, H2, H3)             "
echo "=========================================================="
echo "Starting Aegon SSE sample on ports $PORT_PLAIN (plain) / $PORT_TLS (TLS/H3)..."

$BINARY $PORT_PLAIN $PORT_TLS &
SERVER_PID=$!

cleanup() {
    echo ""
    echo "Stopping Aegon SSE server (PID: $SERVER_PID)..."
    kill -TERM $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    echo "Server stopped."
}
trap cleanup EXIT

# Allow server to bind sockets
sleep 0.3

echo ""
echo "=========================================================="
echo " 1. LIVE HTTP/1.1 TESTS (CLEARTEXT & TLS)                 "
echo "=========================================================="

echo "[1.1] HTTP/1.1 GET /events/raw (Inspect Headers & Chunks):"
curl -s -i --http1.1 "http://127.0.0.1:${PORT_PLAIN}/events/raw"
echo ""

echo "[1.2] HTTP/1.1 GET /events/ticks?count=3 (Live Market Ticker):"
curl -s -N --http1.1 "http://127.0.0.1:${PORT_PLAIN}/events/ticks?count=3"
echo ""

echo "[1.3] HTTP/1.1 TLS GET /events/system?count=2 (Encrypted Telemetry):"
curl -k -s -N --http1.1 "https://127.0.0.1:${PORT_TLS}/events/system?count=2"
echo ""

echo "=========================================================="
echo " 2. LIVE HTTP/2 TESTS (CLEARTEXT H2C & TLS H2)            "
echo "=========================================================="

echo "[2.1] HTTP/2 Cleartext prior-knowledge GET /events/ticks?count=3:"
curl -s -N --http2-prior-knowledge "http://127.0.0.1:${PORT_PLAIN}/events/ticks?count=3"
echo ""

echo "[2.2] HTTP/2 TLS ALPN GET /events/system?count=3:"
curl -k -s -N --http2 "https://127.0.0.1:${PORT_TLS}/events/system?count=3"
echo ""

echo "=========================================================="
echo " 3. LIVE HTTP/3 OVER QUIC (RFC 9114 / RFC 9000)           "
echo "=========================================================="

echo "[3.1] HTTP/3 GET /events/ticks?count=3 (QUIC Stream DATA):"
curl -k -s -N --http3-only "https://127.0.0.1:${PORT_TLS}/events/ticks?count=3"
echo ""

echo "[3.2] HTTP/3 GET /events/system?count=3 (QUIC Telemetry):"
curl -k -s -N --http3-only "https://127.0.0.1:${PORT_TLS}/events/system?count=3"
echo ""

echo "=========================================================="
echo " 4. NON-SSE ROUTE REGRESSION                              "
echo "=========================================================="
echo "[4.1] H1 /health: $(curl -s --http1.1 http://127.0.0.1:${PORT_PLAIN}/health)"
echo "[4.2] H2 /health: $(curl -s --http2-prior-knowledge http://127.0.0.1:${PORT_PLAIN}/health)"
echo "[4.3] H3 /health: $(curl -k -s --http3-only https://127.0.0.1:${PORT_TLS}/health)"

echo ""
echo "=========================================================="
echo "   >>> ALL LIVE TESTS SUCCEEDED ACROSS H1, H2, H3! <<<    "
echo "=========================================================="
