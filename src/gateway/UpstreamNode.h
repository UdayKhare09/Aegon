#pragma once

#include "gateway/GatewayTypes.h"
#include <string>
#include <atomic>
#include <memory>
#include <chrono>

namespace aegon::gateway {

class UpstreamNode;

/**
 * @brief RAII guard tracking active in-flight requests on an upstream node.
 */
class NodeConnectionGuard {
public:
    NodeConnectionGuard() = default;
    explicit NodeConnectionGuard(std::shared_ptr<UpstreamNode> node);
    ~NodeConnectionGuard();

    NodeConnectionGuard(NodeConnectionGuard&& other) noexcept;
    NodeConnectionGuard& operator=(NodeConnectionGuard&& other) noexcept;

    NodeConnectionGuard(const NodeConnectionGuard&) = delete;
    NodeConnectionGuard& operator=(const NodeConnectionGuard&) = delete;

    void release();

private:
    std::shared_ptr<UpstreamNode> node_{nullptr};
};

/**
 * @brief Represents a physical or virtual upstream server node within a cluster.
 */
class UpstreamNode : public std::enable_shared_from_this<UpstreamNode> {
public:
    UpstreamNode(std::string address, uint32_t weight = 1);
    ~UpstreamNode() = default;

    [[nodiscard]] const std::string& address() const noexcept { return address_; }
    [[nodiscard]] const std::string& base_url() const noexcept { return base_url_; }
    [[nodiscard]] uint32_t weight() const noexcept { return weight_; }

    [[nodiscard]] NodeState state() const noexcept {
        return state_.load(std::memory_order_relaxed);
    }

    void set_state(NodeState new_state) noexcept {
        state_.store(new_state, std::memory_order_release);
    }

    [[nodiscard]] bool is_healthy() const noexcept {
        auto s = state();
        return s == NodeState::Healthy || s == NodeState::Suspect || s == NodeState::Recovering;
    }

    [[nodiscard]] uint32_t active_connections() const noexcept {
        return active_connections_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint32_t consecutive_failures() const noexcept {
        return consecutive_failures_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint32_t consecutive_successes() const noexcept {
        return consecutive_successes_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t total_requests() const noexcept {
        return total_requests_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t total_failures() const noexcept {
        return total_failures_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::chrono::steady_clock::time_point last_failure_time() const noexcept {
        auto ms = last_failure_ms_.load(std::memory_order_relaxed);
        return std::chrono::steady_clock::time_point(std::chrono::milliseconds(ms));
    }

    void record_success() noexcept;
    void record_failure() noexcept;

    void acquire_connection() noexcept {
        active_connections_.fetch_add(1, std::memory_order_relaxed);
        total_requests_.fetch_add(1, std::memory_order_relaxed);
    }

    void release_connection() noexcept {
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]] NodeConnectionGuard scoped_connection() {
        return NodeConnectionGuard(shared_from_this());
    }

private:
    std::string address_;
    std::string base_url_;
    uint32_t weight_{1};

    std::atomic<NodeState> state_{NodeState::Healthy};
    std::atomic<uint32_t> active_connections_{0};
    std::atomic<uint32_t> consecutive_failures_{0};
    std::atomic<uint32_t> consecutive_successes_{0};
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_failures_{0};
    std::atomic<int64_t> last_failure_ms_{0};
    std::atomic<int64_t> last_success_ms_{0};
};

} // namespace aegon::gateway
