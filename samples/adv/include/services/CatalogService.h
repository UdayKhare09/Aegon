#pragma once

#include "data/orm/sql/SqlDatabaseClient.h"
#include "data/redis/RedisClient.h"
#include "models/Product.h"
#include "dtos/ProductDto.h"
#include "core/Task.h"
#include <memory>
#include <vector>
#include <optional>

namespace aegon::sample {

class CatalogService {
public:
    explicit CatalogService(data::orm::sql::SqlDatabaseClient& db);

    core::Task<Product> create_product(CreateProductRequest req);
    core::Task<std::optional<Product>> get_product_by_id(int64_t id);
    core::Task<std::vector<Product>> list_products(int page, int limit);
    core::Task<bool> update_stock(int64_t id, int32_t quantity_delta);

private:
    data::orm::sql::SqlDatabaseClient& db_;
};

} // namespace aegon::sample
