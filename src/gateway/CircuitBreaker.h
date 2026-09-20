#pragma once

#include "gateway/GatewayTypes.h"
#include "gateway/UpstreamNode.h"
#include <chrono>

namespace aegon::gateway {

/**
 * @brief Circuit breaker tracking node health and performing fault isolation.
 */
class CircuitBreaker {
public:
    explicit CircuitBreaker(CircuitBreakerConfig config = {});
    ~CircuitBreaker() = default;

    [[nodiscard]] const CircuitBreakerConfig& config() const noexcept { return config_; }

    /**
     * @brief Checks whether a request may be routed to the node.
     * Transitions Dead nodes to Recovering (Half-Open) after cooldown has elapsed.
     */
    bool can_attempt(UpstreamNode& node) const;

    /**
     * @brief Records a successful response, resetting failures and recovering Half-Open nodes.
     */
    void on_success(UpstreamNode& node) const;

    /**
     * @brief Records an upstream failure or 5xx response, tripping circuit when threshold is reached.
     */
    void on_failure(UpstreamNode& node, http::StatusCode status) const;

    /**
     * @brief Determines if an HTTP status code constitutes an upstream failure (e.g. 500, 502, 503, 504).
     */
    static bool is_failure_status(http::StatusCode status) noexcept {
        auto code = static_cast<uint16_t>(status);
        return code >= 500 && code <= 599;
    }

private:
    CircuitBreakerConfig config_;
};

} // namespace aegon::gateway
