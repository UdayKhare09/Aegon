#pragma once

#include "http/Protocol.h"
#include "http/Context.h"
#include "http/Request.h"
#include "http/Response.h"
#include "http/HeaderMap.h"
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>
#include <cstdint>

namespace aegon::gateway {

/**
 * @brief Available load balancing algorithms for Upstream Clusters.
 */
enum class LoadBalancerType : uint8_t {
    RoundRobin,           // Standard sequential round-robin
    WeightedRoundRobin,   // Proportional distribution weighted by node capacity
    LeastConnections,     // Routes to the healthy node with fewest active requests
    ConsistentHash        // Ketama-style virtual node hash ring for sticky sessions
};

/**
 * @brief Source key for Consistent Hashing sticky sessions.
 */
enum class HashKeySource : uint8_t {
    ClientIp,
    Header,
    Cookie
};

/**
 * @brief Health and circuit breaker state of an upstream node.
 */
enum class NodeState : uint8_t {
    Healthy,       // Passing traffic normally
    Suspect,       // Incurring sporadic errors, under observation
    Dead,          // Circuit open or failed health probes; isolated from routing
    Recovering     // Cooldown passed, receiving trial probe requests (Half-Open)
};

constexpr std::string_view to_string(NodeState state) noexcept {
    switch (state) {
        case NodeState::Healthy:    return "Healthy";
        case NodeState::Suspect:    return "Suspect";
        case NodeState::Dead:       return "Dead";
        case NodeState::Recovering: return "Recovering";
    }
    return "Unknown";
}

/**
 * @brief Configuration for active periodic background health probes.
 */
struct HealthCheckConfig {
    std::string path{"/health"};
    std::chrono::seconds interval{10};
    std::chrono::milliseconds timeout{2000};
    uint32_t healthy_threshold{2};
    uint32_t unhealthy_threshold{3};
    bool enabled{false};
};

/**
 * @brief Configuration for passive circuit breaking.
 */
struct CircuitBreakerConfig {
    uint32_t consecutive_errors{5}; // Number of consecutive 5xx/network errors to trip
    std::chrono::seconds cooldown{30}; // Time to wait in Dead state before probe
    bool enabled{true};
};

/**
 * @brief Payload forwarded upstream.
 */
struct UpstreamRequest {
    std::string url;
    http::Method method{http::Method::GET};
    http::HeaderMap headers;
    std::string body;
};

/**
 * @brief Payload received from upstream.
 */
struct UpstreamResponse {
    http::StatusCode status{http::StatusCode::InternalServerError};
    http::HeaderMap headers;
    std::string body;
    std::string node_address;
    std::chrono::microseconds latency{0};

    [[nodiscard]] std::string latency_string() const {
        return std::to_string(latency.count()) + "us";
    }

    [[nodiscard]] std::string latency_ms_str() const {
        double ms = static_cast<double>(latency.count()) / 1000.0;
        return std::to_string(ms) + "ms";
    }
};

/**
 * @brief Options and transformation hooks applied to proxied routes.
 */
struct ProxyOptions {
    std::string strip_prefix{""};
    std::chrono::milliseconds timeout{5000};
    bool pass_host_header{false};
    http::HeaderMap add_request_headers{};

    // Request Transformer hook: invoked before forwarding upstream (provides ctx.req(), ctx.get<T>(), etc.)
    std::function<void(http::Context&, UpstreamRequest&)> on_request{nullptr};

    // Response Transformer hook: invoked before sending response to client
    std::function<void(const UpstreamResponse&, http::Response&)> on_response{nullptr};

    // Fallback Handler hook: invoked when all nodes are dead or circuit open
    std::function<void(http::Context&)> on_fallback{nullptr};
};

} // namespace aegon::gateway
