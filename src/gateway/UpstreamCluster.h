#pragma once

#include "gateway/GatewayTypes.h"
#include "gateway/UpstreamNode.h"
#include "gateway/LoadBalancer.h"
#include "gateway/CircuitBreaker.h"
#include "http/Context.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace aegon::gateway {

/**
 * @brief Manages a pool of upstream nodes, load balancing, health monitoring, and circuit breaking.
 */
class UpstreamCluster : public std::enable_shared_from_this<UpstreamCluster> {
public:
    UpstreamCluster(std::string name,
                    std::unique_ptr<LoadBalancer> lb,
                    CircuitBreakerConfig cb_config = {},
                    HealthCheckConfig hc_config = {});

    ~UpstreamCluster() = default;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const CircuitBreaker& circuit_breaker() const noexcept { return circuit_breaker_; }
    [[nodiscard]] const HealthCheckConfig& health_check_config() const noexcept { return health_check_config_; }

    UpstreamCluster& add_node(std::string address, uint32_t weight = 1);
    UpstreamCluster& add_node(std::shared_ptr<UpstreamNode> node);

    [[nodiscard]] std::vector<std::shared_ptr<UpstreamNode>> nodes() const;

    /**
     * @brief Selects a healthy node using the configured load balancer and circuit breaker.
     */
    [[nodiscard]] std::shared_ptr<UpstreamNode> select_node(const http::Context& ctx);

    void on_request_success(UpstreamNode& node);
    void on_request_failure(UpstreamNode& node, http::StatusCode status);

private:
    std::string name_;
    std::unique_ptr<LoadBalancer> load_balancer_;
    CircuitBreaker circuit_breaker_;
    HealthCheckConfig health_check_config_;

    mutable std::mutex nodes_mutex_;
    std::vector<std::shared_ptr<UpstreamNode>> nodes_;
};

/**
 * @brief Fluent builder for constructing UpstreamCluster instances.
 */
class ClusterBuilder {
public:
    explicit ClusterBuilder(std::string name);

    ClusterBuilder& load_balancer(LoadBalancerType type);
    ClusterBuilder& hash_key(HashKeySource source, std::string key_name = "");
    ClusterBuilder& add_node(std::string address, uint32_t weight = 1);
    ClusterBuilder& health_check(HealthCheckConfig config);
    ClusterBuilder& circuit_breaker(CircuitBreakerConfig config);

    [[nodiscard]] std::shared_ptr<UpstreamCluster> build();

private:
    std::string name_;
    LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
    HashKeySource hash_source_{HashKeySource::ClientIp};
    std::string hash_key_name_{""};
    std::vector<std::pair<std::string, uint32_t>> nodes_;
    HealthCheckConfig health_check_config_{};
    CircuitBreakerConfig circuit_breaker_config_{};
};

} // namespace aegon::gateway
