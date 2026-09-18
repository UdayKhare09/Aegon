#include "handlers/OrderHandler.h"
#include "services/OrderService.h"
#include <glaze/glaze.hpp>

namespace aegon::sample {

core::Task<void> OrderHandler::place_order(http::Context& ctx) {
    auto dto = ctx.bind_json<CreateOrderRequest>();
    if (!dto.has_value()) {
        co_return;
    }

    auto& orderService = ctx.service<OrderService>();
    auto result = co_await orderService.place_order(std::move(*dto));

    if (!result.success || !result.order) {
        ctx.res().status(http::StatusCode::Conflict).json(R"({"error":")" + result.message + R"("})");
        co_return;
    }

    const auto& ord = *result.order;
    OrderResponse resp{
        .id = ord.id,
        .user_id = ord.user_id,
        .product_id = ord.product_id,
        .quantity = ord.quantity,
        .total_amount = ord.total_amount.to_string(),
        .status = ord.status,
        .created_at = ord.created_at.to_iso8601()
    };

    ctx.res().status(http::StatusCode::Created).json(resp);
    co_return;
}

core::Task<void> OrderHandler::get_order(http::Context& ctx) {
    auto id_opt = ctx.req().param("id");
    if (!id_opt || id_opt->empty()) {
        ctx.res().status(http::StatusCode::BadRequest).text("Missing order id");
        co_return;
    }

    int64_t id = 0;
    try {
        id = std::stoll(std::string(*id_opt));
    } catch (...) {
        ctx.res().status(http::StatusCode::BadRequest).text("Invalid order id");
        co_return;
    }

    auto& orderService = ctx.service<OrderService>();
    auto ord = co_await orderService.get_order_by_id(id);

    if (!ord) {
        ctx.res().status(http::StatusCode::NotFound).text("Order not found");
        co_return;
    }

    OrderResponse resp{
        .id = ord->id,
        .user_id = ord->user_id,
        .product_id = ord->product_id,
        .quantity = ord->quantity,
        .total_amount = ord->total_amount.to_string(),
        .status = ord->status,
        .created_at = ord->created_at.to_iso8601()
    };

    ctx.res().json(resp);
    co_return;
}

} // namespace aegon::sample
