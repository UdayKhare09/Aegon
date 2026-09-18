#pragma once

#include "data/orm/sql/Sql.h"
#include "data/types/Decimal.h"
#include "data/types/DateTime.h"
#include "models/Order.h"
#include <string>
#include <cstdint>

namespace aegon::sample {

struct UserProfile {
    int64_t id{0};
    int64_t user_id{0};
    std::string bio;
    std::string shipping_address;

    static auto schema() {
        return data::orm::sql::table<UserProfile>("user_profiles")
            .id(&UserProfile::id)
            .column(&UserProfile::user_id, "user_id")
            .column(&UserProfile::bio, "bio")
            .column(&UserProfile::shipping_address, "shipping_address");
    }
};

struct User {
    int64_t id{0};
    std::string username;
    std::string email;
    data::types::Decimal128 balance{0};
    data::types::DateTime created_at{data::types::DateTime::now()};

    // 1:1 Relation
    data::orm::sql::HasOne<UserProfile> profile;

    // 1:N Relation
    data::orm::sql::HasMany<Order> orders;

    static auto schema() {
        return data::orm::sql::table<User>("users")
            .id(&User::id)
            .column(&User::username, "username")
            .column(&User::email, "email")
            .column(&User::balance, "balance")
            .column(&User::created_at, "created_at")
            .has_one(&User::profile, &UserProfile::user_id)
            .has_many(&User::orders, &Order::user_id);
    }
};

} // namespace aegon::sample
