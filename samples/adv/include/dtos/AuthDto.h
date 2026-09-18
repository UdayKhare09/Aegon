#pragma once

#include "data/validation/Validator.h"
#include "dtos/OrderDto.h"
#include <string>
#include <cstdint>
#include <vector>

namespace aegon::sample {

struct RegisterUserRequest {
    std::string username;
    std::string email;
    double initial_balance{100.0};

    void validate(validation::ValidationRules& v) const {
        v.field("username", username).required().min_len(3).max_len(32);
        v.field("email", email).required().email();
        v.field("initial_balance", initial_balance).min(0.0);
    }
};

struct UserResponse {
    int64_t id{0};
    std::string username;
    std::string email;
    std::string balance;
    std::string created_at;
    std::vector<OrderResponse> orders;
};

} // namespace aegon::sample
