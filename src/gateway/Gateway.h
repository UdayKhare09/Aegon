#pragma once

#include "gateway/GatewayTypes.h"
#include "gateway/UpstreamNode.h"
#include "gateway/LoadBalancer.h"
#include "gateway/CircuitBreaker.h"
#include "gateway/UpstreamCluster.h"
#include "gateway/ProxyHandler.h"

namespace aegon::gateway {

// Convenience namespace alias / re-exports
using Cluster = UpstreamCluster;
using Node = UpstreamNode;

} // namespace aegon::gateway
