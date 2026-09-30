#pragma once

#include "core/Task.h"
#include <functional>
#include <string>
#include <string_view>
#include <chrono>
#include <glaze/glaze.hpp>

namespace aegon::http {

/**
 * @brief Write-channel function injected by the connection layer into SseStream.
 *
 * Each connection (H1/H2/H3) provides its own implementation that writes
 * a raw SSE frame directly to the wire (chunked on H1, DATA frame on H2/H3).
 */
using SseWriteFn = std::function<core::Task<void>(std::string)>;

/**
 * @brief Streaming writer for Server-Sent Events (RFC 8895).
 *
 * Returned by ctx.sse(). The developer co_awaits individual events;
 * each call immediately flushes to the client.
 *
 * Works identically on HTTP/1.1 (chunked), HTTP/2 (DATA frames),
 * and HTTP/3 (QUIC stream DATA).
 *
 * Wire format per event:
 *   [event: <name>\n]
 *   data: <data>\n
 *   \n
 */
class SseStream {
public:
    explicit SseStream(SseWriteFn write_fn) noexcept
        : write_fn_(std::move(write_fn)) {}

    /**
     * @brief Send a raw SSE frame as-is. Caller is responsible for correct formatting.
     */
    core::Task<void> send(std::string_view raw) {
        if (!open_ || !write_fn_) co_return;
        co_await write_fn_(std::string(raw));
    }

    /**
     * @brief Send a named event with data.
     * Emits: "event: <name>\ndata: <data>\n\n"
     */
    core::Task<void> event(std::string_view name, std::string_view data) {
        if (!open_ || !write_fn_) co_return;
        std::string frame;
        frame.reserve(name.size() + data.size() + 16);
        frame.append("event: ");
        frame.append(name);
        frame.push_back('\n');
        frame.append("data: ");
        frame.append(data);
        frame.append("\n\n");
        co_await write_fn_(std::move(frame));
    }

    /**
     * @brief Send a data-only event (no event name).
     * Emits: "data: <data>\n\n"
     */
    core::Task<void> data(std::string_view value) {
        if (!open_ || !write_fn_) co_return;
        std::string frame;
        frame.reserve(value.size() + 8);
        frame.append("data: ");
        frame.append(value);
        frame.append("\n\n");
        co_await write_fn_(std::move(frame));
    }

    /**
     * @brief Send a JSON-serialized event.
     * Emits: "event: <name>\ndata: <json>\n\n"
     */
    template <typename T>
    core::Task<void> send_json(std::string_view name, const T& val) {
        if (!open_ || !write_fn_) co_return;
        std::string json;
        (void)glz::write_json(val, json);
        co_await event(name, json);
    }

    /**
     * @brief Send a comment line (keepalive / metadata).
     * Emits: ": <text>\n\n"
     */
    core::Task<void> comment(std::string_view text) {
        if (!open_ || !write_fn_) co_return;
        std::string frame;
        frame.reserve(text.size() + 4);
        frame.append(": ");
        frame.append(text);
        frame.append("\n\n");
        co_await write_fn_(std::move(frame));
    }

    /**
     * @brief Advise clients how long to wait before reconnecting.
     * Emits: "retry: <ms>\n\n"
     */
    core::Task<void> retry(std::chrono::milliseconds ms) {
        if (!open_ || !write_fn_) co_return;
        std::string frame = "retry: " + std::to_string(ms.count()) + "\n\n";
        co_await write_fn_(std::move(frame));
    }

    core::Task<void> retry(uint32_t ms) {
        co_await retry(std::chrono::milliseconds(ms));
    }

    /**
     * @brief Mark the stream closed. Subsequent sends are no-ops.
     * The connection layer is responsible for sending the final EOF / zero-chunk.
     */
    void close() noexcept { open_ = false; }

    [[nodiscard]] bool is_open() const noexcept { return open_; }

private:
    SseWriteFn write_fn_;
    bool open_{true};
};

} // namespace aegon::http
