#pragma once

#include "gateway/GatewayTypes.h"
#include "gateway/UpstreamNode.h"
#include "http/Context.h"
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <atomic>
#include <mutex>
#include <map>

namespace aegon::gateway {

/**
 * @brief Abstract interface for cluster load balancing strategies.
 */
class LoadBalancer {
public:
    virtual ~LoadBalancer() = default;

    /**
     * @brief Selects an upstream node for the incoming request.
     * @param nodes Pool of nodes in the cluster.
     * @param ctx Incoming HTTP request context.
     * @return Selected node, or nullptr if no suitable node is available.
     */
    virtual std::shared_ptr<UpstreamNode> select_node(
        const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
        const http::Context& ctx) = 0;
};

/**
 * @brief Sequential round-robin load balancer.
 */
class RoundRobinBalancer : public LoadBalancer {
public:
    RoundRobinBalancer() = default;

    std::shared_ptr<UpstreamNode> select_node(
        const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
        const http::Context& ctx) override;

private:
    std::atomic<uint64_t> counter_{0};
};

/**
 * @brief Smooth weighted round-robin balancer based on Nginx's algorithm.
 */
class WeightedRoundRobinBalancer : public LoadBalancer {
public:
    WeightedRoundRobinBalancer() = default;

    std::shared_ptr<UpstreamNode> select_node(
        const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
        const http::Context& ctx) override;

private:
    std::mutex mutex_;
    std::map<std::string, int64_t> current_weights_;
};

/**
 * @brief Least connections load balancer (routes to node with minimum active requests).
 */
class LeastConnectionsBalancer : public LoadBalancer {
public:
    LeastConnectionsBalancer() = default;

    std::shared_ptr<UpstreamNode> select_node(
        const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
        const http::Context& ctx) override;
};

/**
 * @brief Consistent hash ring for sticky sessions (Client IP, Header, or Cookie).
 */
class ConsistentHashRing : public LoadBalancer {
public:
    ConsistentHashRing(HashKeySource source = HashKeySource::ClientIp,
                       std::string key_name = "",
                       uint32_t virtual_nodes_per_host = 160);

    std::shared_ptr<UpstreamNode> select_node(
        const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
        const http::Context& ctx) override;

    void rebuild_ring(const std::vector<std::shared_ptr<UpstreamNode>>& nodes);

    [[nodiscard]] HashKeySource source() const noexcept { return source_; }
    [[nodiscard]] const std::string& key_name() const noexcept { return key_name_; }

    static uint32_t hash_fnv1a(std::string_view key) noexcept;

private:
    std::string extract_key(const http::Context& ctx) const;

    HashKeySource source_{HashKeySource::ClientIp};
    std::string key_name_{""};
    uint32_t virtual_nodes_{160};

    std::mutex ring_mutex_;
    // Sorted list of (hash, node)
    std::vector<std::pair<uint32_t, std::shared_ptr<UpstreamNode>>> ring_;
    size_t last_nodes_count_{0};
};

/**
 * @brief Factory function creating a LoadBalancer instance.
 */
std::unique_ptr<LoadBalancer> create_load_balancer(
    LoadBalancerType type,
    HashKeySource hash_source = HashKeySource::ClientIp,
    std::string hash_key_name = "");

} // namespace aegon::gateway
