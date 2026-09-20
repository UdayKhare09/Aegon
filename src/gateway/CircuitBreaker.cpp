#include "gateway/CircuitBreaker.h"

namespace aegon::gateway {

CircuitBreaker::CircuitBreaker(CircuitBreakerConfig config)
    : config_(config) {}

bool CircuitBreaker::can_attempt(UpstreamNode& node) const {
    if (!config_.enabled) return true;

    auto s = node.state();
    if (s == NodeState::Healthy || s == NodeState::Suspect) {
        return true;
    }

    if (s == NodeState::Dead) {
        auto elapsed = std::chrono::steady_clock::now() - node.last_failure_time();
        if (elapsed >= config_.cooldown) {
            // Cooldown expired: transition to Half-Open (Recovering) to allow a probe
            node.set_state(NodeState::Recovering);
            return true;
        }
        return false;
    }

    if (s == NodeState::Recovering) {
        // Allow trial request
        return true;
    }

    return true;
}

void CircuitBreaker::on_success(UpstreamNode& node) const {
    node.record_success();
}

void CircuitBreaker::on_failure(UpstreamNode& node, http::StatusCode /*status*/) const {
    node.record_failure();

    if (!config_.enabled) return;

    auto s = node.state();
    if (s == NodeState::Recovering) {
        // Probe request failed: immediately trip back to Dead
        node.set_state(NodeState::Dead);
        return;
    }

    if (node.consecutive_failures() >= config_.consecutive_errors) {
        node.set_state(NodeState::Dead);
    } else if (node.consecutive_failures() > 0 && s == NodeState::Healthy) {
        node.set_state(NodeState::Suspect);
    }
}

} // namespace aegon::gateway
