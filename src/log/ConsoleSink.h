#pragma once

#include "log/Sink.h"
#include <mutex>

namespace aegon::log {

enum class ColorMode : uint8_t {
    Auto,   // Enabled if stdout is a TTY
    Always, // Always emit ANSI escape codes
    Never   // Never emit ANSI escape codes
};

struct ConsoleSinkConfig {
    ColorMode color_mode{ColorMode::Auto};
    bool show_location{true};
    bool show_thread_id{false};
    bool stderr_for_errors{false};
};

class ConsoleSink : public ISink {
public:
    explicit ConsoleSink(ConsoleSinkConfig config = {});
    ~ConsoleSink() override = default;

    void log(const Record& record) override;
    void flush() override;

private:
    ConsoleSinkConfig config_;
    bool use_colors_{false};
    std::mutex mutex_;
};

} // namespace aegon::log
