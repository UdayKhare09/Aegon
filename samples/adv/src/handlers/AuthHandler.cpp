#include "handlers/AuthHandler.h"
#include "models/User.h"
#include "dtos/AuthDto.h"
#include "data/orm/sql/SqlDatabaseClient.h"
#include <glaze/glaze.hpp>

namespace aegon::sample {

core::Task<void> AuthHandler::register_user(http::Context& ctx) {
    auto dto = ctx.bind_json<RegisterUserRequest>();
    if (!dto.has_value()) {
        co_return; // bind_json already sets 422 Unprocessable Entity with RFC 7807 problem details
    }

    auto& db = ctx.service<data::orm::sql::SqlDatabaseClient>();

    User u;
    u.username = std::move(dto->username);
    u.email = std::move(dto->email);
    u.balance = *data::types::Decimal128::from_string(std::to_string(dto->initial_balance));
    u.created_at = data::types::DateTime::now();

    int64_t new_id = co_await db.insert_get_id(u);
    u.id = new_id;

    UserResponse resp{
        .id = u.id,
        .username = u.username,
        .email = u.email,
        .balance = u.balance.to_string(),
        .created_at = u.created_at.to_iso8601()
    };

    ctx.res().status(http::StatusCode::Created).json(resp);
    co_return;
}

core::Task<void> AuthHandler::get_user(http::Context& ctx) {
    auto id_opt = ctx.req().param("id");
    if (!id_opt || id_opt->empty()) {
        ctx.res().status(http::StatusCode::BadRequest).text("Missing user id");
        co_return;
    }

    int64_t id = 0;
    try {
        id = std::stoll(std::string(*id_opt));
    } catch (...) {
        ctx.res().status(http::StatusCode::BadRequest).text("Invalid user id format");
        co_return;
    }

    auto& db = ctx.service<data::orm::sql::SqlDatabaseClient>();
    auto users = co_await db.fetch_all(
        db.from<User>()
          .where(&User::id, data::orm::sql::Op::Eq, id)
          .include(&User::orders)
    );

    if (users.empty()) {
        ctx.res().status(http::StatusCode::NotFound).text("User not found");
        co_return;
    }

    const auto& user = users[0];
    UserResponse resp{
        .id = user.id,
        .username = user.username,
        .email = user.email,
        .balance = user.balance.to_string(),
        .created_at = user.created_at.to_iso8601(),
        .orders = {}
    };

    if (user.orders.is_loaded()) {
        for (const auto& ord : user.orders) {
            resp.orders.push_back(OrderResponse{
                .id = ord.id,
                .user_id = ord.user_id,
                .product_id = ord.product_id,
                .quantity = ord.quantity,
                .total_amount = ord.total_amount.to_string(),
                .status = ord.status,
                .created_at = ord.created_at.to_iso8601()
            });
        }
    }

    ctx.res().json(resp);
    co_return;
}

} // namespace aegon::sample
