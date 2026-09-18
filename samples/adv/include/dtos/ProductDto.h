#pragma once

#include "data/validation/Validator.h"
#include <string>
#include <cstdint>

namespace aegon::sample {

struct CreateProductRequest {
    std::string sku;
    std::string name;
    double price{0.0};
    int32_t stock{0};

    void validate(validation::ValidationRules& v) const {
        v.field("sku", sku).required().min_len(3).max_len(32);
        v.field("name", name).required().min_len(2).max_len(100);
        v.field("price", price).min(0.01);
        v.field("stock", stock).min(0);
    }
};

struct ProductResponse {
    int64_t id{0};
    std::string sku;
    std::string name;
    std::string price;
    int32_t stock{0};
    int64_t version{1};
};

struct ProductListQuery {
    int page{1};
    int limit{20};

    void validate(validation::ValidationRules& v) const {
        v.field("page", page).min(1);
        v.field("limit", limit).min(1).max(100);
    }
};

} // namespace aegon::sample
