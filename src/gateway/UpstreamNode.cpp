#include "gateway/UpstreamNode.h"

namespace aegon::gateway {

NodeConnectionGuard::NodeConnectionGuard(std::shared_ptr<UpstreamNode> node)
    : node_(std::move(node)) {
    if (node_) {
        node_->acquire_connection();
    }
}

NodeConnectionGuard::~NodeConnectionGuard() {
    release();
}

NodeConnectionGuard::NodeConnectionGuard(NodeConnectionGuard&& other) noexcept
    : node_(std::move(other.node_)) {
    other.node_ = nullptr;
}

NodeConnectionGuard& NodeConnectionGuard::operator=(NodeConnectionGuard&& other) noexcept {
    if (this != &other) {
        release();
        node_ = std::move(other.node_);
        other.node_ = nullptr;
    }
    return *this;
}

void NodeConnectionGuard::release() {
    if (node_) {
        node_->release_connection();
        node_ = nullptr;
    }
}

UpstreamNode::UpstreamNode(std::string address, uint32_t weight)
    : address_(std::move(address)), weight_(weight > 0 ? weight : 1) {
    if (address_.starts_with("http://") || address_.starts_with("https://")) {
        base_url_ = address_;
    } else {
        base_url_ = "http://" + address_;
    }
}

void UpstreamNode::record_success() noexcept {
    consecutive_failures_.store(0, std::memory_order_relaxed);
    consecutive_successes_.fetch_add(1, std::memory_order_relaxed);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    last_success_ms_.store(now_ms, std::memory_order_relaxed);

    if (state_.load(std::memory_order_relaxed) == NodeState::Recovering) {
        // Promoted from Recovering (Half-Open) to Healthy
        state_.store(NodeState::Healthy, std::memory_order_release);
    }
}

void UpstreamNode::record_failure() noexcept {
    consecutive_successes_.store(0, std::memory_order_relaxed);
    consecutive_failures_.fetch_add(1, std::memory_order_relaxed);
    total_failures_.fetch_add(1, std::memory_order_relaxed);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    last_failure_ms_.store(now_ms, std::memory_order_relaxed);
}

} // namespace aegon::gateway
