#include "handlers/LeaderboardHandler.h"
#include "services/LeaderboardService.h"
#include <glaze/glaze.hpp>

namespace aegon::sample {

core::Task<void> LeaderboardHandler::get_top_products(http::Context& ctx) {
    size_t limit = 10;
    auto limit_query = ctx.req().query("limit");
    if (limit_query) {
        try {
            int l = std::stoi(std::string(*limit_query));
            if (l > 0) limit = static_cast<size_t>(l);
        } catch (...) {}
    }

    auto& leaderboard = ctx.service<LeaderboardService>();
    auto resp = co_await leaderboard.get_top_products(limit);

    ctx.res().json(resp);
    co_return;
}

} // namespace aegon::sample
