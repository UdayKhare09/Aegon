# Cloud-Native API Gateway & Reverse Proxy

Aegon provides a dedicated, high-performance **Cloud-Native API Gateway & Reverse Proxy** engine (`aegon_gateway`). Built in modern C++26 on top of Linux `io_uring` and Aegon's `HttpClient`, it terminates client traffic, handles intelligent load balancing, isolates failing upstreams with circuit breakers, and streams responses upstream with zero extraneous overhead.

---

## Key Features

- **100% Pure C++ Fluent API**: Direct programmatic route mounting on `Server` and `RouteGroup`.
- **4 Load Balancing Algorithms**:
  - `RoundRobin`: Sequential rotation among healthy nodes.
  - `WeightedRoundRobin`: Smooth deficit distribution proportional to node weights.
  - `LeastConnections`: Routes dynamically to the node with the fewest active coroutines / in-flight requests.
  - `ConsistentHash`: 160 virtual nodes per host on a hash ring for sticky sessions (Client IP, Header, or Cookie).
- **Dual Circuit Breaker & Health Checks**:
  - **Passive Circuit Breaking**: Automatically trips nodes to `Dead` upon encountering consecutive 5xx/network errors.
  - **Half-Open Recovery**: Permits a trial probe after a configurable cooldown period.
  - **Active Health Checks**: Periodic background `/health` pings to detect node recovery.
- **Native Vector of Middlewares**: Pass `std::vector<MiddlewareFn>` directly to proxy routes to enforce CORS, security headers, JWT verification, and RBAC *before* upstream network calls.
- **Request & Response Transformers**: Rewrite headers, inject user claims (`X-User-Id`), strip hop-by-hop headers, and track upstream latencies.
- **Fallback Handlers**: Custom graceful degradation logic when upstream clusters are exhausted or circuits are open.

---

## Defining Upstream Clusters

Clusters represent logical services composed of one or more upstream host nodes:

```cpp
#include "gateway/Gateway.h"

using namespace aegon::gateway;

// 1. Cluster with Least-Connections and Circuit Breaking
auto billing_cluster = ClusterBuilder("billing-service")
    .load_balancer(LoadBalancerType::LeastConnections)
    .add_node("10.0.1.10:8080", /*weight=*/5)
    .add_node("10.0.1.11:8080", /*weight=*/10)
    .health_check({
        .path = "/health",
        .interval = std::chrono::seconds(10),
        .timeout = std::chrono::milliseconds(2000),
        .healthy_threshold = 2,
        .unhealthy_threshold = 3
    })
    .circuit_breaker({
        .consecutive_errors = 5,              // Trip after 5 consecutive 5xx errors
        .cooldown = std::chrono::seconds(30)  // Wait 30s before trial probe
    })
    .build();

// 2. Cluster with Consistent Hashing (Sticky Sessions)
auto session_cluster = ClusterBuilder("user-sessions")
    .load_balancer(LoadBalancerType::ConsistentHash)
    .hash_key(HashKeySource::Header, "X-User-Id") // or ClientIp, Cookie
    .add_node("10.0.2.1:8080")
    .add_node("10.0.2.2:8080")
    .build();
```

---

## Mounting Reverse Proxy Routes

Proxy routes can be mounted directly on `Server` or inside a `RouteGroup`:

```cpp
#include "http/Server.h"
#include "http/middleware/Cors.h"
#include "http/middleware/SecurityHeaders.h"
#include "http/middleware/Auth.h"
#include "http/middleware/RbacGuard.h"

using namespace aegon::http;
using namespace aegon::http::middleware;

int main() {
    Server server;
    server.listen(8080);

    // Prepare route middlewares as a native std::vector<MiddlewareFn>
    auto verifier = jwt::JwtVerifier::create_hs256("super-secret-key-32-chars-long!");
    std::vector<MiddlewareFn> billing_middlewares = {
        cors(CorsConfig::permissive()),
        security_headers(SecurityHeadersConfig::secure()),
        jwt_auth(verifier),
        rbac_guard("billing:admin")
    };

    // Mount proxy route directly on Server
    server.proxy(
        "/api/v1/billing/*",
        billing_cluster,
        ProxyOptions{
            .strip_prefix = "/api/v1/billing", // /api/v1/billing/pay -> /pay upstream
            .timeout = std::chrono::seconds(5),

            // Request Transformer: inject user ID from Context and forward headers
            .on_request = [](Context& ctx, UpstreamRequest& upstream_req) {
                if (auto* user_id = ctx.get<std::string>()) {
                    upstream_req.headers.set("X-User-Id", *user_id);
                }
                auto host_hdr = ctx.req().header("host");
                upstream_req.headers.set("X-Forwarded-Host", host_hdr ? std::string(*host_hdr) : "localhost");
            },

            // Response Transformer: sanitize headers and inject latency
            .on_response = [](const UpstreamResponse& upstream_res, Response& client_res) {
                client_res.headers.erase("Server");
                client_res.header("X-Upstream-Node", upstream_res.node_address);
                client_res.header("X-Proxy-Latency", upstream_res.latency_string());
            },

            // Fallback Handler: triggered if all nodes are dead or circuit open
            .on_fallback = [](Context& ctx) {
                ctx.res().status(StatusCode::ServiceUnavailable).json({
                    {"error", "Service Temporarily Unavailable"},
                    {"cluster", "billing-service"},
                    {"code", "UPSTREAM_CIRCUIT_OPEN"}
                });
            }
        },
        billing_middlewares // Passed directly as vector
    );

    // Route Group Mounting
    auto v2 = server.group("/api/v2");
    v2.proxy("/sessions/*", session_cluster, ProxyOptions{
        .strip_prefix = "/api/v2/sessions"
    });

    server.run();
    return 0;
}
```

---

## Load Balancing Strategies

| Strategy | Description | Best For |
| :--- | :--- | :--- |
| `RoundRobin` | Alternates requests sequentially among healthy nodes. | Homogeneous upstream servers with uniform request workloads. |
| `WeightedRoundRobin` | Smooth deficit weighted distribution respecting node capacities. | Upstream clusters with unequal server sizing (e.g. 16-core vs 4-core). |
| `LeastConnections` | Routes to the healthy node currently handling the fewest active coroutine requests. | Uneven or long-running requests (e.g. large report generation, file uploads). |
| `ConsistentHash` | 160 virtual nodes per host ring. Hashes Client IP, Header, or Cookie to route to the same upstream node. | Sticky user sessions, in-memory cache locality, and stateful WebSockets. |

---

## Circuit Breaker Lifecycle

Nodes transition through four distinct states during failures and recoveries:

```
          [0 errors]
  ┌─────────────────────────┐
  │         Healthy         │ ◄────────────────────────┐
  └────────────┬────────────┘                          │
               │ 1st Error                             │ Probe Success
               ▼                                       │
  ┌─────────────────────────┐                          │
  │         Suspect         │                          │
  └────────────┬────────────┘                          │
               │ Consecutive Errors >= Threshold       │
               ▼                                       │
  ┌─────────────────────────┐   Cooldown Elapsed   ┌───┴─────────────────────┐
  │          Dead           │ ───────────────────> │       Recovering        │
  └─────────────────────────┘                      │       (Half-Open)       │
               ▲                                   └───────────┬─────────────┘
               │               Probe Failed                    │
               └───────────────────────────────────────────────┘
```

1. **`Healthy`**: Processing traffic normally.
2. **`Suspect`**: 1 to $N-1$ errors detected. Still eligible for traffic, under observation.
3. **`Dead`**: Consecutive failures reached threshold. Breaker trips, isolating the node immediately from routing.
4. **`Recovering` (Half-Open)**: After the `cooldown` duration expires, the node is allowed a single trial request. If successful, it is promoted back to `Healthy`; if it fails, it trips immediately back to `Dead`.
