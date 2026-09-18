#include "handlers/CatalogHandler.h"
#include "services/CatalogService.h"
#include <glaze/glaze.hpp>

namespace aegon::sample {

core::Task<void> CatalogHandler::create_product(http::Context& ctx) {
    auto dto = ctx.bind_json<CreateProductRequest>();
    if (!dto.has_value()) {
        co_return;
    }

    auto& catalog = ctx.service<CatalogService>();
    Product created = co_await catalog.create_product(std::move(*dto));

    ProductResponse resp{
        .id = created.id,
        .sku = created.sku,
        .name = created.name,
        .price = created.price.to_string(),
        .stock = created.stock,
        .version = created.version
    };

    ctx.res().status(http::StatusCode::Created).json(resp);
    co_return;
}

core::Task<void> CatalogHandler::get_product(http::Context& ctx) {
    auto id_opt = ctx.req().param("id");
    if (!id_opt || id_opt->empty()) {
        ctx.res().status(http::StatusCode::BadRequest).text("Missing product id");
        co_return;
    }

    int64_t id = 0;
    try {
        id = std::stoll(std::string(*id_opt));
    } catch (...) {
        ctx.res().status(http::StatusCode::BadRequest).text("Invalid product id");
        co_return;
    }

    auto& catalog = ctx.service<CatalogService>();
    auto product = co_await catalog.get_product_by_id(id);

    if (!product) {
        ctx.res().status(http::StatusCode::NotFound).text("Product not found");
        co_return;
    }

    ProductResponse resp{
        .id = product->id,
        .sku = product->sku,
        .name = product->name,
        .price = product->price.to_string(),
        .stock = product->stock,
        .version = product->version
    };

    ctx.res().json(resp);
    co_return;
}

core::Task<void> CatalogHandler::list_products(http::Context& ctx) {
    int page = 1;
    int limit = 20;

    auto page_query = ctx.req().query("page");
    if (page_query) {
        try { page = std::stoi(std::string(*page_query)); } catch (...) {}
    }
    auto limit_query = ctx.req().query("limit");
    if (limit_query) {
        try { limit = std::stoi(std::string(*limit_query)); } catch (...) {}
    }

    auto& catalog = ctx.service<CatalogService>();
    auto products = co_await catalog.list_products(page, limit);

    std::vector<ProductResponse> resp;
    resp.reserve(products.size());
    for (const auto& p : products) {
        resp.push_back(ProductResponse{
            .id = p.id,
            .sku = p.sku,
            .name = p.name,
            .price = p.price.to_string(),
            .stock = p.stock,
            .version = p.version
        });
    }

    ctx.res().json(resp);
    co_return;
}

} // namespace aegon::sample
