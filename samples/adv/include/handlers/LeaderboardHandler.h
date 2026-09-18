#pragma once

#include "http/Context.h"
#include "core/Task.h"

namespace aegon::sample {

class LeaderboardHandler {
public:
    static core::Task<void> get_top_products(http::Context& ctx);
};

} // namespace aegon::sample
