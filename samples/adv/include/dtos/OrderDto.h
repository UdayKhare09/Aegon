#pragma once

#include "data/validation/Validator.h"
#include <string>
#include <cstdint>
#include <vector>

namespace aegon::sample {

struct CreateOrderRequest {
    int64_t user_id{0};
    int64_t product_id{0};
    int32_t quantity{1};

    void validate(validation::ValidationRules& v) const {
        v.field("user_id", user_id).min(1);
        v.field("product_id", product_id).min(1);
        v.field("quantity", quantity).min(1).max(100);
    }
};

struct OrderResponse {
    int64_t id{0};
    int64_t user_id{0};
    int64_t product_id{0};
    int32_t quantity{0};
    std::string total_amount;
    std::string status;
    std::string created_at;
};

struct LeaderboardEntry {
    int64_t product_id{0};
    std::string sku;
    double sales_volume{0.0};
};

struct LeaderboardResponse {
    std::vector<LeaderboardEntry> top_products;
};

} // namespace aegon::sample
