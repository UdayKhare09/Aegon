#pragma once

#include "http/Context.h"
#include "core/Task.h"

namespace aegon::sample {

class OrderHandler {
public:
    static core::Task<void> place_order(http::Context& ctx);
    static core::Task<void> get_order(http::Context& ctx);
};

} // namespace aegon::sample
