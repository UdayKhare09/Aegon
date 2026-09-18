#include "services/CatalogService.h"

namespace aegon::sample {

CatalogService::CatalogService(data::orm::sql::SqlDatabaseClient& db)
    : db_(db) {}

core::Task<Product> CatalogService::create_product(CreateProductRequest req) {
    Product p;
    p.sku = std::move(req.sku);
    p.name = std::move(req.name);
    p.price = *data::types::Decimal128::from_string(std::to_string(req.price));
    p.stock = req.stock;
    p.version = 1;

    // Automatic SQL cache insertion via declarative TableDef::cache()
    int64_t new_id = co_await db_.insert_get_id(p);
    p.id = new_id;

    co_return p;
}

core::Task<std::optional<Product>> CatalogService::get_product_by_id(int64_t id) {
    // Declarative cache lookup: SqlDatabaseClient checks cache first,
    // queries SQL on miss, and writes through transparently
    co_return co_await db_.find_by_id<Product>(id);
}

core::Task<std::vector<Product>> CatalogService::list_products(int page, int limit) {
    int64_t offset = static_cast<int64_t>(std::max(0, page - 1)) * limit;
    // Two-phase query pointer caching with table epoch validation
    auto query = db_.from<Product>().limit(limit).offset(offset).cached();
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
    // Automatic cache invalidation and table epoch bump on mutation
    size_t updated_rows = co_await db_.update_entity(*prod);
    co_return (updated_rows > 0);
}

} // namespace aegon::sample
