#pragma once

#include <string>
#include <cstdint>

namespace aegon::sample {

struct AppConfig {
    std::string host{"0.0.0.0"};
    uint16_t port{8080};
    size_t worker_threads{1};
    
    // Database configuration
    std::string db_type{"sqlite"}; // "sqlite" or "postgres"
    std::string db_path{":memory:"};
    std::string pg_conninfo{"host=127.0.0.1 port=5432 dbname=aegon_test user=postgres password=postgres"};
    
    // Redis configuration
    std::string redis_host{"127.0.0.1"};
    uint16_t redis_port{6379};
    size_t redis_pool_size{4};
};

} // namespace aegon::sample
