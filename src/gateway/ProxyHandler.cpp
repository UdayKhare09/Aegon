#include "gateway/ProxyHandler.h"
#include <chrono>
#include <algorithm>

namespace aegon::gateway {

namespace {

inline bool is_hop_by_hop(std::string_view name) noexcept {
    static const std::string_view hop_headers[] = {
        "connection", "keep-alive", "proxy-authenticate", "proxy-authorization",
        "te", "trailer", "transfer-encoding", "upgrade", "proxy-connection"
    };

    for (auto h : hop_headers) {
        if (name.size() == h.size()) {
            bool match = true;
            for (size_t i = 0; i < name.size(); ++i) {
                char c1 = name[i];
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c1 != h[i]) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}

} // namespace

ProxyHandler::ProxyHandler(std::shared_ptr<UpstreamCluster> cluster, ProxyOptions options)
    : cluster_(std::move(cluster)), options_(std::move(options)) {
    http::client::ClientConfig client_cfg;
    client_cfg.timeout = options_.timeout;
    client_cfg.connect_timeout = std::min(options_.timeout, std::chrono::milliseconds(3000));
    client_cfg.follow_redirects = false; // Proxy forwards redirects directly to client
    client_ = std::make_shared<http::client::HttpClient>(client_cfg);
}

core::Task<void> ProxyHandler::operator()(http::Context& ctx) {
    if (!cluster_) {
        ctx.res().status(http::StatusCode::InternalServerError)
                 .header("Content-Type", "application/json")
                 .body(R"({"error":"Internal Server Error","reason":"No upstream cluster configured"})");
        co_return;
    }

    auto node = cluster_->select_node(ctx);
    if (!node) {
        if (options_.on_fallback) {
            options_.on_fallback(ctx);
        } else {
            ctx.res().status(http::StatusCode::ServiceUnavailable)
                     .header("Content-Type", "application/json")
                     .body(R"({"error":"Service Unavailable","reason":"No healthy upstream node in cluster"})");
        }
        co_return;
    }

    // Inflight connection count RAII guard
    auto conn_guard = node->scoped_connection();

    // Construct upstream URL path
    std::string path(ctx.req().path());
    if (!options_.strip_prefix.empty() && path.starts_with(options_.strip_prefix)) {
        path = path.substr(options_.strip_prefix.size());
    }
    if (path.empty() || path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    if (!ctx.req().query().empty()) {
        path += "?";
        path += ctx.req().query();
    }

    UpstreamRequest ureq;
    ureq.url = node->base_url() + path;
    ureq.method = ctx.req().method();
    ureq.body = std::string(ctx.req().body());

    // Copy client request headers, filtering hop-by-hop
    for (const auto& [name, val] : ctx.req().headers()) {
        if (!is_hop_by_hop(name)) {
            ureq.headers.set(name, val);
        }
    }

    // Host header handling
    if (!options_.pass_host_header) {
        ureq.headers.set("Host", node->address());
    }

    // Forwarding headers
    auto client_ip = ctx.req().header("x-forwarded-for");
    if (client_ip && !client_ip->empty()) {
        ureq.headers.set("X-Forwarded-For", std::string(*client_ip));
    } else {
        ureq.headers.set("X-Forwarded-For", "127.0.0.1");
    }
    auto host_hdr = ctx.req().header("host");
    ureq.headers.set("X-Forwarded-Host", host_hdr ? std::string(*host_hdr) : "localhost");
    ureq.headers.set("X-Forwarded-Proto", "http");

    // Apply configured request headers
    for (const auto& [name, val] : options_.add_request_headers) {
        ureq.headers.set(name, val);
    }

    // Invoke user Request Transformer hook
    if (options_.on_request) {
        options_.on_request(ctx, ureq);
    }

    // Prepare HTTP Client request
    auto req_builder = client_->request(ureq.method, ureq.url);
    for (const auto& [name, val] : ureq.headers) {
        req_builder.header(name, val);
    }
    if (!ureq.body.empty()) {
        req_builder.body(ureq.body);
    }
    req_builder.timeout(options_.timeout);

    auto start_time = std::chrono::steady_clock::now();
    http::Response upstream_res;

    try {
        upstream_res = req_builder.send_sync();
    } catch (const std::exception& e) {
        cluster_->on_request_failure(*node, http::StatusCode::BadGateway);
        if (options_.on_fallback) {
            options_.on_fallback(ctx);
        } else {
            ctx.res().status(http::StatusCode::BadGateway)
                     .header("Content-Type", "application/json")
                     .body(R"({"error":"Bad Gateway","details":")" + std::string(e.what()) + R"("})");
        }
        co_return;
    }

    auto elapsed = std::chrono::steady_clock::now() - start_time;

    // Report result to cluster circuit breaker
    if (CircuitBreaker::is_failure_status(upstream_res.status())) {
        cluster_->on_request_failure(*node, upstream_res.status());
    } else {
        cluster_->on_request_success(*node);
    }

    // Propagate upstream response to client
    ctx.res().status(upstream_res.status());
    for (const auto& [name, val] : upstream_res.headers()) {
        if (!is_hop_by_hop(name)) {
            ctx.res().header(name, val);
        }
    }
    ctx.res().body(std::string(upstream_res.body()));

    UpstreamResponse ures{
        .status = upstream_res.status(),
        .headers = upstream_res.headers(),
        .body = std::string(upstream_res.body()),
        .node_address = node->address(),
        .latency = std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
    };

    // Invoke user Response Transformer hook
    if (options_.on_response) {
        options_.on_response(ures, ctx.res());
    }

    co_return;
}

http::Handler make_proxy_handler(
    std::shared_ptr<UpstreamCluster> cluster,
    ProxyOptions options) {
    auto handler = std::make_shared<ProxyHandler>(std::move(cluster), std::move(options));
    return [handler](http::Context& ctx) -> core::Task<void> {
        co_await (*handler)(ctx);
    };
}

} // namespace aegon::gateway
