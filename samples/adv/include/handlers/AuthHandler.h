#pragma once

#include "http/Context.h"
#include "core/Task.h"

namespace aegon::sample {

class AuthHandler {
public:
    static core::Task<void> register_user(http::Context& ctx);
    static core::Task<void> get_user(http::Context& ctx);
};

} // namespace aegon::sample
