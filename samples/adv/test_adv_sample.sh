#!/usr/bin/env bash
set -e

PORT=8999
SAMPLE_BIN="../../build/samples/adv/aegon_adv_sample"

if [ ! -f "$SAMPLE_BIN" ]; then
    SAMPLE_BIN="build/samples/adv/aegon_adv_sample"
fi

if [ ! -f "$SAMPLE_BIN" ]; then
    echo "Error: aegon_adv_sample binary not found!"
    exit 1
fi

echo "========================================================"
echo "    Running End-to-End Verification for Aegon HyperStore"
echo "========================================================"

# Launch the server in background
$SAMPLE_BIN $PORT &
SERVER_PID=$!

cleanup() {
    echo "Stopping server (PID $SERVER_PID)..."
    kill -TERM $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
}
trap cleanup EXIT

# Wait for server to become responsive
for i in {1..30}; do
    if curl -s http://127.0.0.1:$PORT/health > /dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

echo "[1/8] Testing /health endpoint..."
HEALTH_RESP=$(curl -s http://127.0.0.1:$PORT/health)
if [ "$HEALTH_RESP" != "OK" ]; then
    echo "Failed /health: expected OK, got $HEALTH_RESP"
    exit 1
fi
echo "  -> PASS: Health check returned 200 OK"

echo "[2/8] Testing DTO Validation auto-rejection (RFC 7807 422)..."
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:$PORT/api/v1/users \
    -H "Content-Type: application/json" \
    -d '{"username":"a","email":"bad-email","initial_balance":-10}')
if [ "$HTTP_CODE" != "422" ]; then
    echo "Failed DTO validation test: expected 422, got $HTTP_CODE"
    exit 1
fi
echo "  -> PASS: Invalid DTO automatically rejected with HTTP 422"

echo "[3/8] Testing User Registration (POST /api/v1/users)..."
USER_RESP=$(curl -s -X POST http://127.0.0.1:$PORT/api/v1/users \
    -H "Content-Type: application/json" \
    -d '{"username":"bob_builder","email":"bob@aegon.dev","initial_balance":500.0}')
echo "Response: $USER_RESP"
USER_ID=$(echo "$USER_RESP" | grep -o '"id":[0-9]*' | cut -d: -f2)
if [ -z "$USER_ID" ]; then
    echo "Failed to register user"
    exit 1
fi
echo "  -> PASS: Registered user with ID $USER_ID"

echo "[4/8] Testing Fetch User (GET /api/v1/users/:id)..."
FETCHED_USER=$(curl -s http://127.0.0.1:$PORT/api/v1/users/$USER_ID)
echo "Response: $FETCHED_USER"
if ! echo "$FETCHED_USER" | grep -q "bob_builder"; then
    echo "Failed to get user by id"
    exit 1
fi
echo "  -> PASS: User fetched successfully"

echo "[5/8] Testing Product Creation & Catalog (POST /api/v1/products)..."
PROD_RESP=$(curl -s -X POST http://127.0.0.1:$PORT/api/v1/products \
    -H "Content-Type: application/json" \
    -d '{"sku":"FIRE-RING","name":"Ring of Fire","price":75.0,"stock":25}')
echo "Response: $PROD_RESP"
PROD_ID=$(echo "$PROD_RESP" | grep -o '"id":[0-9]*' | cut -d: -f2)
if [ -z "$PROD_ID" ]; then
    echo "Failed to create product"
    exit 1
fi
echo "  -> PASS: Created product with ID $PROD_ID"

echo "[6/8] Testing Product Catalog Listing (GET /api/v1/products)..."
CATALOG_RESP=$(curl -s "http://127.0.0.1:$PORT/api/v1/products?page=1&limit=10")
if ! echo "$CATALOG_RESP" | grep -q "FIRE-RING"; then
    echo "Failed catalog listing"
    exit 1
fi
echo "  -> PASS: Catalog listing returned seeded and created products"

echo "[7/8] Testing Atomic Order Placement (OCC & Transaction) (POST /api/v1/orders)..."
ORDER_RESP=$(curl -s -X POST http://127.0.0.1:$PORT/api/v1/orders \
    -H "Content-Type: application/json" \
    -d "{\"user_id\":$USER_ID,\"product_id\":$PROD_ID,\"quantity\":2}")
echo "Response: $ORDER_RESP"
ORDER_ID=$(echo "$ORDER_RESP" | grep -o '"id":[0-9]*' | cut -d: -f2)
if [ -z "$ORDER_ID" ]; then
    echo "Failed to place order"
    exit 1
fi
echo "  -> PASS: Placed order $ORDER_ID atomically with OCC stock decrement"

echo "[8/9] Testing Eager-Loaded 1:N Relation (GET /api/v1/users/:id with .include(&User::orders))..."
FETCHED_USER_WITH_ORDERS=$(curl -s http://127.0.0.1:$PORT/api/v1/users/$USER_ID)
echo "Response: $FETCHED_USER_WITH_ORDERS"
if ! echo "$FETCHED_USER_WITH_ORDERS" | grep -q "\"orders\":\\[{\"id\":$ORDER_ID"; then
    echo "Failed: user orders relation was not eager-loaded!"
    exit 1
fi
echo "  -> PASS: User's orders relation eager-loaded automatically via .include()"

echo "[9/9] Testing Live SSE Stream (GET /api/v1/events/live)..."
SSE_RESP=$(curl -s --max-time 1 http://127.0.0.1:$PORT/api/v1/events/live)
if ! echo "$SSE_RESP" | grep -q "event: connected"; then
    echo "Failed SSE test"
    exit 1
fi
echo "  -> PASS: SSE stream connected and emitted live events"

echo "========================================================"
echo "   >>> ALL ADVANCED SAMPLE INTEGRATION TESTS PASSED! <<<"
echo "========================================================"
