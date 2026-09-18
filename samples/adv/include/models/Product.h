#pragma once

#include "data/orm/sql/Sql.h"
#include "data/types/Decimal.h"
#include "models/Order.h"
#include <string>
#include <cstdint>

namespace aegon::sample {

struct Product {
    int64_t id{0};
    std::string sku;
    std::string name;
    data::types::Decimal128 price{0};
    int32_t stock{0};
    int64_t version{1}; // Optimistic Concurrency Control (OCC)

    // 1:N Relation: all orders for this product
    data::orm::sql::HasMany<Order> orders;

    static auto schema() {
        return data::orm::sql::table<Product>("products")
            .id(&Product::id)
            .column(&Product::sku, "sku")
            .column(&Product::name, "name")
            .column(&Product::price, "price")
            .column(&Product::stock, "stock")
            .version(&Product::version, "version")
            .has_many(&Product::orders, &Order::product_id);
    }
};

} // namespace aegon::sample
