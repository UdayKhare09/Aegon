#pragma once

#include "http/Context.h"
#include "core/Task.h"

namespace aegon::sample {

class StreamHandler {
public:
    static core::Task<void> live_events(http::Context& ctx);
};

} // namespace aegon::sample
