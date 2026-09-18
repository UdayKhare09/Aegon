#pragma once

#include "http/Context.h"
#include "core/Task.h"

namespace aegon::sample {

class CatalogHandler {
public:
    static core::Task<void> create_product(http::Context& ctx);
    static core::Task<void> get_product(http::Context& ctx);
    static core::Task<void> list_products(http::Context& ctx);
};

} // namespace aegon::sample
