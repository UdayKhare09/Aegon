#include "handlers/StreamHandler.h"
#include "data/types/DateTime.h"

namespace aegon::sample {

core::Task<void> StreamHandler::live_events(http::Context& ctx) {
    ctx.res()
        .header("Content-Type", "text/event-stream")
        .header("Cache-Control", "no-cache")
        .header("Connection", "keep-alive");

    std::string sse_payload =
        "event: connected\n"
        "data: {\"status\":\"ok\",\"timestamp\":\"" + data::types::DateTime::now().to_iso8601() + "\"}\n\n";

    ctx.res().text(sse_payload);
    co_return;
}

} // namespace aegon::sample
