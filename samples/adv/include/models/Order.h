#pragma once

#include "data/orm/sql/Sql.h"
#include "data/types/Decimal.h"
#include "data/types/DateTime.h"
#include <string>
#include <cstdint>

namespace aegon::sample {

struct Order {
    int64_t id{0};
    int64_t user_id{0};
    int64_t product_id{0};
    int32_t quantity{0};
    data::types::Decimal128 total_amount{0};
    std::string status{"PENDING"}; // PENDING, CONFIRMED, REJECTED
    data::types::DateTime created_at{data::types::DateTime::now()};

    static auto schema() {
        return data::orm::sql::table<Order>("orders")
            .id(&Order::id)
            .column(&Order::user_id, "user_id")
            .column(&Order::product_id, "product_id")
            .column(&Order::quantity, "quantity")
            .column(&Order::total_amount, "total_amount")
            .column(&Order::status, "status")
            .column(&Order::created_at, "created_at");
    }
};

} // namespace aegon::sample
