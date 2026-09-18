#include "services/CatalogService.h"
#include "services/RedisProvider.h"
#include <glaze/glaze.hpp>
#include <iostream>

namespace aegon::sample {

CatalogService::CatalogService(data::orm::sql::SqlDatabaseClient& db,
                               std::shared_ptr<data::redis::RedisClient> redis)
    : db_(db), redis_(std::move(redis)) {}

core::Task<Product> CatalogService::create_product(CreateProductRequest req) {
    Product p;
    p.sku = std::move(req.sku);
    p.name = std::move(req.name);
    p.price = *data::types::Decimal128::from_string(std::to_string(req.price));
    p.stock = req.stock;
    p.version = 1;

    int64_t new_id = co_await db_.insert_get_id(p);
    p.id = new_id;

    auto* redis = redis_ ? redis_.get() : get_current_redis_client();
    if (redis) {
        std::string json_str;
        (void)glz::write_json(p, json_str);
        (void)co_await redis->set("product:" + std::to_string(p.id), json_str, std::chrono::seconds(120));
    }

    co_return p;
}

core::Task<std::optional<Product>> CatalogService::get_product_by_id(int64_t id) {
    std::string cache_key = "product:" + std::to_string(id);
    auto* redis = redis_ ? redis_.get() : get_current_redis_client();

    // 1. Check Redis L2 cache if available
    if (redis) {
        auto cached = co_await redis->get(cache_key);
        if (cached && !cached->empty()) {
            Product prod;
            auto ec = glz::read_json(prod, *cached);
            if (!ec) {
                co_return prod;
            }
        }
    }

    // 2. Query relational database via ORM
    auto prod = co_await db_.find_by_id<Product>(id);
    if (prod && redis) {
        // Cache aside with 120s TTL
        std::string json_str;
        (void)glz::write_json(*prod, json_str);
        (void)co_await redis->set(cache_key, json_str, std::chrono::seconds(120));
    }

    co_return prod;
}

core::Task<std::vector<Product>> CatalogService::list_products(int page, int limit) {
    int64_t offset = static_cast<int64_t>(std::max(0, page - 1)) * limit;
    auto query = db_.from<Product>().limit(limit).offset(offset);
    co_return co_await db_.fetch_all(query);
}

core::Task<bool> CatalogService::update_stock(int64_t id, int32_t quantity_delta) {
    auto prod = co_await db_.find_by_id<Product>(id);
    if (!prod) {
        co_return false;
    }

    if (prod->stock + quantity_delta < 0) {
        co_return false;
    }

    prod->stock += quantity_delta;
    size_t updated_rows = co_await db_.update_entity(*prod);
    bool ok = (updated_rows > 0);

    auto* redis = redis_ ? redis_.get() : get_current_redis_client();
    if (ok && redis) {
        (void)co_await redis->del("product:" + std::to_string(id));
    }

    co_return ok;
}

} // namespace aegon::sample
