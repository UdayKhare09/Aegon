#pragma once

#include "gateway/GatewayTypes.h"
#include "gateway/UpstreamCluster.h"
#include "http/Context.h"
#include "http/Middleware.h"
#include "http/client/HttpClient.h"
#include "core/Task.h"
#include <memory>

namespace aegon::gateway {

/**
 * @brief Asynchronous reverse proxy handler executing request forwarding,
 * load balancing, circuit breaking, and response transformation.
 */
class ProxyHandler {
public:
    ProxyHandler(std::shared_ptr<UpstreamCluster> cluster, ProxyOptions options);
    ~ProxyHandler() = default;

    core::Task<void> operator()(http::Context& ctx);

    [[nodiscard]] const UpstreamCluster& cluster() const noexcept { return *cluster_; }
    [[nodiscard]] const ProxyOptions& options() const noexcept { return options_; }

private:
    std::shared_ptr<UpstreamCluster> cluster_;
    ProxyOptions options_;
    std::shared_ptr<http::client::HttpClient> client_;
};

/**
 * @brief Helper creating a standard Aegon Handler function wrapping a ProxyHandler.
 */
http::Handler make_proxy_handler(
    std::shared_ptr<UpstreamCluster> cluster,
    ProxyOptions options = {});

} // namespace aegon::gateway
