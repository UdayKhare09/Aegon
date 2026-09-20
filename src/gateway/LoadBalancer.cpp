#include "gateway/LoadBalancer.h"
#include <algorithm>
#include <limits>

namespace aegon::gateway {

// ============================================================================
// RoundRobinBalancer
// ============================================================================
std::shared_ptr<UpstreamNode> RoundRobinBalancer::select_node(
    const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
    const http::Context& /*ctx*/) {
    if (nodes.empty()) return nullptr;

    std::vector<std::shared_ptr<UpstreamNode>> healthy;
    healthy.reserve(nodes.size());
    for (const auto& n : nodes) {
        if (n && n->is_healthy()) {
            healthy.push_back(n);
        }
    }

    if (healthy.empty()) return nullptr;

    uint64_t idx = counter_.fetch_add(1, std::memory_order_relaxed);
    return healthy[idx % healthy.size()];
}

// ============================================================================
// WeightedRoundRobinBalancer
// ============================================================================
std::shared_ptr<UpstreamNode> WeightedRoundRobinBalancer::select_node(
    const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
    const http::Context& /*ctx*/) {
    if (nodes.empty()) return nullptr;

    std::vector<std::shared_ptr<UpstreamNode>> healthy;
    healthy.reserve(nodes.size());
    for (const auto& n : nodes) {
        if (n && n->is_healthy()) {
            healthy.push_back(n);
        }
    }

    if (healthy.empty()) return nullptr;
    if (healthy.size() == 1) return healthy.front();

    std::lock_guard<std::mutex> lock(mutex_);
    int64_t total_weight = 0;
    std::shared_ptr<UpstreamNode> best_node = nullptr;
    int64_t max_current = std::numeric_limits<int64_t>::min();

    for (const auto& node : healthy) {
        int64_t effective_weight = static_cast<int64_t>(node->weight());
        int64_t& current = current_weights_[node->address()];
        current += effective_weight;
        total_weight += effective_weight;

        if (current > max_current) {
            max_current = current;
            best_node = node;
        }
    }

    if (best_node) {
        current_weights_[best_node->address()] -= total_weight;
    }

    return best_node;
}

// ============================================================================
// LeastConnectionsBalancer
// ============================================================================
std::shared_ptr<UpstreamNode> LeastConnectionsBalancer::select_node(
    const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
    const http::Context& /*ctx*/) {
    if (nodes.empty()) return nullptr;

    std::shared_ptr<UpstreamNode> best_node = nullptr;
    uint32_t min_connections = std::numeric_limits<uint32_t>::max();
    uint64_t min_total_requests = std::numeric_limits<uint64_t>::max();

    for (const auto& node : nodes) {
        if (!node || !node->is_healthy()) continue;

        uint32_t active = node->active_connections();
        uint64_t total = node->total_requests();

        if (active < min_connections || (active == min_connections && total < min_total_requests)) {
            min_connections = active;
            min_total_requests = total;
            best_node = node;
        }
    }

    return best_node;
}

// ============================================================================
// ConsistentHashRing
// ============================================================================
ConsistentHashRing::ConsistentHashRing(HashKeySource source, std::string key_name, uint32_t virtual_nodes_per_host)
    : source_(source), key_name_(std::move(key_name)), virtual_nodes_(virtual_nodes_per_host > 0 ? virtual_nodes_per_host : 160) {}

uint32_t ConsistentHashRing::hash_fnv1a(std::string_view key) noexcept {
    uint32_t hash = 2166136261u;
    for (char c : key) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

std::string ConsistentHashRing::extract_key(const http::Context& ctx) const {
    switch (source_) {
        case HashKeySource::ClientIp: {
            auto xff = ctx.req().header("x-forwarded-for");
            if (xff && !xff->empty()) {
                size_t comma = xff->find(',');
                return std::string(comma != std::string_view::npos ? xff->substr(0, comma) : *xff);
            }
            auto real_ip = ctx.req().header("x-real-ip");
            if (real_ip && !real_ip->empty()) return std::string(*real_ip);
            return "127.0.0.1";
        }
        case HashKeySource::Header: {
            if (!key_name_.empty()) {
                auto h = ctx.req().header(key_name_);
                if (h && !h->empty()) return std::string(*h);
            }
            return "";
        }
        case HashKeySource::Cookie: {
            if (!key_name_.empty()) {
                auto c = ctx.req().cookie(key_name_);
                if (c && !c->empty()) return std::string(*c);
            }
            return "";
        }
    }
    return "";
}

void ConsistentHashRing::rebuild_ring(const std::vector<std::shared_ptr<UpstreamNode>>& nodes) {
    ring_.clear();
    for (const auto& node : nodes) {
        if (!node) continue;
        uint32_t count = virtual_nodes_ * (node->weight() > 0 ? node->weight() : 1);
        for (uint32_t i = 0; i < count; ++i) {
            std::string vnode_key = node->address() + "#" + std::to_string(i);
            uint32_t hash = hash_fnv1a(vnode_key);
            ring_.emplace_back(hash, node);
        }
    }
    std::sort(ring_.begin(), ring_.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    last_nodes_count_ = nodes.size();
}

std::shared_ptr<UpstreamNode> ConsistentHashRing::select_node(
    const std::vector<std::shared_ptr<UpstreamNode>>& nodes,
    const http::Context& ctx) {
    if (nodes.empty()) return nullptr;

    std::lock_guard<std::mutex> lock(ring_mutex_);
    if (ring_.empty() || nodes.size() != last_nodes_count_) {
        rebuild_ring(nodes);
    }
    if (ring_.empty()) return nullptr;

    std::string key = extract_key(ctx);
    if (key.empty()) {
        // Fallback to client address or first healthy node
        key = "default_fallback_key";
    }

    uint32_t h = hash_fnv1a(key);

    auto it = std::lower_bound(
        ring_.begin(), ring_.end(), h,
        [](const std::pair<uint32_t, std::shared_ptr<UpstreamNode>>& elem, uint32_t val) {
            return elem.first < val;
        });

    if (it == ring_.end()) {
        it = ring_.begin();
    }

    // Walk clockwise to find the first healthy node
    size_t ring_size = ring_.size();
    size_t start_idx = static_cast<size_t>(std::distance(ring_.begin(), it));

    for (size_t step = 0; step < ring_size; ++step) {
        size_t cur_idx = (start_idx + step) % ring_size;
        const auto& cand = ring_[cur_idx].second;
        if (cand && cand->is_healthy()) {
            return cand;
        }
    }

    return nullptr;
}

// ============================================================================
// Factory
// ============================================================================
std::unique_ptr<LoadBalancer> create_load_balancer(
    LoadBalancerType type,
    HashKeySource hash_source,
    std::string hash_key_name) {
    switch (type) {
        case LoadBalancerType::RoundRobin:
            return std::make_unique<RoundRobinBalancer>();
        case LoadBalancerType::WeightedRoundRobin:
            return std::make_unique<WeightedRoundRobinBalancer>();
        case LoadBalancerType::LeastConnections:
            return std::make_unique<LeastConnectionsBalancer>();
        case LoadBalancerType::ConsistentHash:
            return std::make_unique<ConsistentHashRing>(hash_source, std::move(hash_key_name));
    }
    return std::make_unique<RoundRobinBalancer>();
}

} // namespace aegon::gateway
