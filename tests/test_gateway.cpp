#include "gateway/Gateway.h"
#include "http/Server.h"
#include "http/Context.h"
#include "http/Request.h"
#include "http/Response.h"
#include "http/Middleware.h"
#include "core/Task.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <map>

using namespace aegon;
using namespace aegon::http;
using namespace aegon::gateway;

void test_round_robin_balancer() {
    std::cout << "[TEST 1] Round-Robin Load Balancer..." << std::endl;

    auto cluster = ClusterBuilder("service-rr")
        .load_balancer(LoadBalancerType::RoundRobin)
        .add_node("10.0.0.1:8080")
        .add_node("10.0.0.2:8080")
        .add_node("10.0.0.3:8080")
        .build();

    Request req;
    Response res;
    Context ctx(req, res);

    std::vector<std::string> expected = {
        "10.0.0.1:8080", "10.0.0.2:8080", "10.0.0.3:8080",
        "10.0.0.1:8080", "10.0.0.2:8080", "10.0.0.3:8080"
    };

    for (const auto& exp : expected) {
        auto node = cluster->select_node(ctx);
        assert(node != nullptr);
        assert(node->address() == exp);
    }

    std::cout << "  -> PASS\n";
}

void test_weighted_round_robin_balancer() {
    std::cout << "[TEST 2] Weighted Round-Robin (Smooth Deficit)..." << std::endl;

    auto cluster = ClusterBuilder("service-wrr")
        .load_balancer(LoadBalancerType::WeightedRoundRobin)
        .add_node("10.0.0.1:8080", /*weight=*/5)
        .add_node("10.0.0.2:8080", /*weight=*/1)
        .add_node("10.0.0.3:8080", /*weight=*/1)
        .build();

    Request req;
    Response res;
    Context ctx(req, res);

    std::map<std::string, int> counts;
    for (int i = 0; i < 70; ++i) {
        auto node = cluster->select_node(ctx);
        assert(node != nullptr);
        counts[node->address()]++;
    }

    assert(counts["10.0.0.1:8080"] == 50);
    assert(counts["10.0.0.2:8080"] == 10);
    assert(counts["10.0.0.3:8080"] == 10);

    std::cout << "  -> PASS\n";
}

void test_least_connections_balancer() {
    std::cout << "[TEST 3] Least-Connections Load Balancer..." << std::endl;

    auto cluster = ClusterBuilder("service-lc")
        .load_balancer(LoadBalancerType::LeastConnections)
        .add_node("node-busy:8080")
        .add_node("node-idle:8080")
        .build();

    auto nodes = cluster->nodes();
    assert(nodes.size() == 2);
    auto node_busy = nodes[0];
    auto node_idle = nodes[1];

    // Simulate 5 active connections on node_busy
    node_busy->acquire_connection();
    node_busy->acquire_connection();
    node_busy->acquire_connection();
    node_busy->acquire_connection();
    node_busy->acquire_connection();

    Request req;
    Response res;
    Context ctx(req, res);

    // Should choose node_idle
    auto chosen1 = cluster->select_node(ctx);
    assert(chosen1 == node_idle);

    // Simulate node_idle becoming busier than node_busy
    for (int i = 0; i < 10; ++i) {
        node_idle->acquire_connection();
    }

    // Now node_busy (5 conns) should be chosen over node_idle (10 conns)
    auto chosen2 = cluster->select_node(ctx);
    assert(chosen2 == node_busy);

    std::cout << "  -> PASS\n";
}

void test_consistent_hash_ring() {
    std::cout << "[TEST 4] Consistent Hashing Sticky Sessions..." << std::endl;

    auto cluster = ClusterBuilder("service-hash")
        .load_balancer(LoadBalancerType::ConsistentHash)
        .hash_key(HashKeySource::Header, "X-User-Id")
        .add_node("node-a:8080")
        .add_node("node-b:8080")
        .add_node("node-c:8080")
        .build();

    Request req1;
    req1.headers().set("X-User-Id", "user-12345");
    Response res1;
    Context ctx1(req1, res1);

    auto target1 = cluster->select_node(ctx1);
    assert(target1 != nullptr);

    // Repeated requests with same user ID MUST hit the same node
    for (int i = 0; i < 20; ++i) {
        auto n = cluster->select_node(ctx1);
        assert(n == target1);
    }

    // Another user maps deterministically
    Request req2;
    req2.headers().set("X-User-Id", "user-99999");
    Response res2;
    Context ctx2(req2, res2);

    auto target2 = cluster->select_node(ctx2);
    assert(target2 != nullptr);

    for (int i = 0; i < 20; ++i) {
        auto n = cluster->select_node(ctx2);
        assert(n == target2);
    }

    // Test failover when target node dies
    target1->set_state(NodeState::Dead);
    auto failover_node = cluster->select_node(ctx1);
    assert(failover_node != nullptr);
    assert(failover_node != target1); // Routed to next healthy clockwise node

    std::cout << "  -> PASS\n";
}

void test_circuit_breaker_isolation_and_recovery() {
    std::cout << "[TEST 5] Circuit Breaker Trip & Recovery (Half-Open)..." << std::endl;

    auto cluster = ClusterBuilder("service-cb")
        .load_balancer(LoadBalancerType::RoundRobin)
        .add_node("node-flaky:8080")
        .add_node("node-backup:8080")
        .circuit_breaker({
            .consecutive_errors = 3,
            .cooldown = std::chrono::seconds(1) // 1 second cooldown for fast test
        })
        .build();

    auto nodes = cluster->nodes();
    auto flaky = nodes[0];
    auto backup = nodes[1];

    assert(flaky->state() == NodeState::Healthy);
    assert(flaky->is_healthy());

    // 1st error -> Suspect
    cluster->on_request_failure(*flaky, StatusCode::InternalServerError);
    assert(flaky->state() == NodeState::Suspect);
    assert(flaky->is_healthy());

    // 2nd error -> Suspect
    cluster->on_request_failure(*flaky, StatusCode::BadGateway);
    assert(flaky->state() == NodeState::Suspect);

    // 3rd error -> Breaker TRIPS to Dead!
    cluster->on_request_failure(*flaky, StatusCode::ServiceUnavailable);
    assert(flaky->state() == NodeState::Dead);
    assert(!flaky->is_healthy());

    Request req;
    Response res;
    Context ctx(req, res);

    // Cluster should now isolate the flaky node and return the backup node
    for (int i = 0; i < 5; ++i) {
        auto chosen = cluster->select_node(ctx);
        assert(chosen == backup);
    }

    // Wait for cooldown to expire (simulate time elapsed)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // can_attempt should now transition to Recovering (Half-Open)
    assert(cluster->circuit_breaker().can_attempt(*flaky) == true);
    assert(flaky->state() == NodeState::Recovering);

    // Probe succeeds -> Promoted back to Healthy!
    cluster->on_request_success(*flaky);
    assert(flaky->state() == NodeState::Healthy);
    assert(flaky->is_healthy());

    std::cout << "  -> PASS\n";
}

void test_connection_guard_raii() {
    std::cout << "[TEST 6] NodeConnectionGuard RAII In-Flight Tracking..." << std::endl;

    auto node = std::make_shared<UpstreamNode>("127.0.0.1:9000");
    assert(node->active_connections() == 0);
    assert(node->total_requests() == 0);

    {
        auto guard1 = node->scoped_connection();
        assert(node->active_connections() == 1);
        assert(node->total_requests() == 1);

        {
            auto guard2 = node->scoped_connection();
            assert(node->active_connections() == 2);
            assert(node->total_requests() == 2);
        }

        assert(node->active_connections() == 1);
    }

    assert(node->active_connections() == 0);
    assert(node->total_requests() == 2);

    std::cout << "  -> PASS\n";
}

void test_proxy_options_and_transformers() {
    std::cout << "[TEST 7] ProxyOptions, Transformers & Fallback Handlers..." << std::endl;

    bool fallback_called = false;
    ProxyOptions options{
        .strip_prefix = "/api/v1/billing",
        .on_request = [](Context& ctx, UpstreamRequest& upstream_req) {
            upstream_req.headers.set("X-Custom-Client", "Aegon-Proxy");
            if (auto* uid = ctx.get<std::string>()) {
                upstream_req.headers.set("X-User-Id", *uid);
            }
        },
        .on_response = [](const UpstreamResponse& ures, Response& client_res) {
            client_res.header("X-Proxy-Latency", ures.latency_string());
        },
        .on_fallback = [&fallback_called](Context& ctx) {
            fallback_called = true;
            ctx.res().status(StatusCode::ServiceUnavailable).body("Fallback Active");
        }
    };

    // Test empty cluster triggers fallback
    auto empty_cluster = ClusterBuilder("empty-service").build();
    auto handler = make_proxy_handler(empty_cluster, options);

    Request req;
    Response res;
    Context ctx(req, res);

    auto task = handler(ctx);
    task.resume();

    assert(fallback_called);
    assert(res.status() == StatusCode::ServiceUnavailable);
    assert(res.body() == "Fallback Active");

    std::cout << "  -> PASS\n";
}

void test_server_and_middleware_vector() {
    std::cout << "[TEST 8] Server & Group Proxy Integration with std::vector<MiddlewareFn>..." << std::endl;

    Server server;
    server.listen(9999);

    auto cluster = ClusterBuilder("billing-upstream")
        .add_node("10.0.1.5:8080")
        .build();

    bool mw1_called = false;
    bool mw2_called = false;

    std::vector<MiddlewareFn> middlewares = {
        [&mw1_called](Context& ctx, Next next) -> core::Task<void> {
            mw1_called = true;
            co_await next(ctx);
        },
        [&mw2_called](Context& ctx, Next next) -> core::Task<void> {
            mw2_called = true;
            co_await next(ctx);
        }
    };

    // Mount proxy directly on server
    server.proxy("/api/v1/billing/*", cluster, ProxyOptions{
        .strip_prefix = "/api/v1/billing"
    }, middlewares);

    // Route Group proxy mounting
    auto api = server.group("/api/v2");
    api.proxy("/orders/*", cluster, ProxyOptions{
        .strip_prefix = "/api/v2/orders"
    }, middlewares);

    // Match and execute route via router
    Request req;
    req.set_method(Method::GET);
    req.set_path("/api/v1/billing/invoices/123");
    auto match = server.router().match(req);

    assert(match.route_found);
    assert(match.handler != nullptr);

    Response res;
    Context ctx(req, res);

    auto task = (*match.handler)(ctx);
    task.resume();

    // Verify both middlewares executed!
    assert(mw1_called);
    assert(mw2_called);

    std::cout << "  -> PASS\n";
}

void test_middleware_rejection_short_circuit() {
    std::cout << "[TEST 9] Middleware Rejection Short-Circuit (401 Unauthorized)..." << std::endl;

    Server server;
    server.listen(9998);

    auto cluster = ClusterBuilder("secure-upstream")
        .add_node("10.0.1.5:8080")
        .build();

    // Middleware that rejects request if token is missing
    std::vector<MiddlewareFn> auth_mw = {
        [](Context& ctx, Next next) -> core::Task<void> {
            auto auth = ctx.req().header("Authorization");
            if (!auth || auth->empty() || *auth != "Bearer secret_token") {
                ctx.res().status(StatusCode::Unauthorized).body("Unauthorized");
                co_return; // SHORT-CIRCUIT: next(ctx) is never called!
            }
            co_await next(ctx);
        }
    };

    server.proxy("/secure/*", cluster, ProxyOptions{}, auth_mw);

    // 1. Unauthorized request
    Request req1;
    req1.set_method(Method::GET);
    req1.set_path("/secure/data");
    auto match1 = server.router().match(req1);
    assert(match1.route_found);

    Response res1;
    Context ctx1(req1, res1);
    auto task1 = (*match1.handler)(ctx1);
    task1.resume();

    assert(res1.status() == StatusCode::Unauthorized);
    assert(res1.body() == "Unauthorized");

    std::cout << "  -> PASS\n";
}

int main() {
    std::cout << "\n============================================\n";
    std::cout << "   RUNNING AEGON GATEWAY TEST SUITE         \n";
    std::cout << "============================================\n\n";

    test_round_robin_balancer();
    test_weighted_round_robin_balancer();
    test_least_connections_balancer();
    test_consistent_hash_ring();
    test_circuit_breaker_isolation_and_recovery();
    test_connection_guard_raii();
    test_proxy_options_and_transformers();
    test_server_and_middleware_vector();
    test_middleware_rejection_short_circuit();

    std::cout << "\n============================================\n";
    std::cout << "   ALL GATEWAY TESTS PASSED (100%)          \n";
    std::cout << "============================================\n\n";
    return 0;
}
