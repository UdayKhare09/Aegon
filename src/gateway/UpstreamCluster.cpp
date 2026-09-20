#include "gateway/UpstreamCluster.h"

namespace aegon::gateway {

UpstreamCluster::UpstreamCluster(
    std::string name,
    std::unique_ptr<LoadBalancer> lb,
    CircuitBreakerConfig cb_config,
    HealthCheckConfig hc_config)
    : name_(std::move(name)),
      load_balancer_(std::move(lb)),
      circuit_breaker_(cb_config),
      health_check_config_(hc_config) {
    if (!load_balancer_) {
        load_balancer_ = std::make_unique<RoundRobinBalancer>();
    }
}

UpstreamCluster& UpstreamCluster::add_node(std::string address, uint32_t weight) {
    return add_node(std::make_shared<UpstreamNode>(std::move(address), weight));
}

UpstreamCluster& UpstreamCluster::add_node(std::shared_ptr<UpstreamNode> node) {
    if (!node) return *this;
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    nodes_.push_back(std::move(node));
    return *this;
}

std::vector<std::shared_ptr<UpstreamNode>> UpstreamCluster::nodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    return nodes_;
}

std::shared_ptr<UpstreamNode> UpstreamCluster::select_node(const http::Context& ctx) {
    std::vector<std::shared_ptr<UpstreamNode>> current_nodes;
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        current_nodes = nodes_;
    }

    if (current_nodes.empty()) return nullptr;

    // First attempt using the configured load balancer
    auto selected = load_balancer_->select_node(current_nodes, ctx);
    if (selected && circuit_breaker_.can_attempt(*selected)) {
        return selected;
    }

    // Fallback: look for any healthy node that circuit breaker permits
    for (const auto& node : current_nodes) {
        if (node && node->is_healthy() && circuit_breaker_.can_attempt(*node)) {
            return node;
        }
    }

    return nullptr;
}

void UpstreamCluster::on_request_success(UpstreamNode& node) {
    circuit_breaker_.on_success(node);
}

void UpstreamCluster::on_request_failure(UpstreamNode& node, http::StatusCode status) {
    circuit_breaker_.on_failure(node, status);
}

// ============================================================================
// ClusterBuilder
// ============================================================================
ClusterBuilder::ClusterBuilder(std::string name)
    : name_(std::move(name)) {}

ClusterBuilder& ClusterBuilder::load_balancer(LoadBalancerType type) {
    lb_type_ = type;
    return *this;
}

ClusterBuilder& ClusterBuilder::hash_key(HashKeySource source, std::string key_name) {
    hash_source_ = source;
    hash_key_name_ = std::move(key_name);
    return *this;
}

ClusterBuilder& ClusterBuilder::add_node(std::string address, uint32_t weight) {
    nodes_.emplace_back(std::move(address), weight);
    return *this;
}

ClusterBuilder& ClusterBuilder::health_check(HealthCheckConfig config) {
    config.enabled = true;
    health_check_config_ = std::move(config);
    return *this;
}

ClusterBuilder& ClusterBuilder::circuit_breaker(CircuitBreakerConfig config) {
    circuit_breaker_config_ = std::move(config);
    return *this;
}

std::shared_ptr<UpstreamCluster> ClusterBuilder::build() {
    auto lb = create_load_balancer(lb_type_, hash_source_, hash_key_name_);
    auto cluster = std::make_shared<UpstreamCluster>(
        std::move(name_),
        std::move(lb),
        circuit_breaker_config_,
        health_check_config_);

    for (auto& [addr, weight] : nodes_) {
        cluster->add_node(std::move(addr), weight);
    }

    return cluster;
}

} // namespace aegon::gateway
