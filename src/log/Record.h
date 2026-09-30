#pragma once

#include "log/Level.h"
#include <chrono>
#include <source_location>
#include <string>
#include <string_view>
#include <cstdint>

namespace aegon::log {

/**
 * @brief Represents a single log event captured by the logger.
 */
struct Record {
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    Level level{Level::Info};
    std::string message;
    std::source_location location{std::source_location::current()};
    uint64_t thread_id{0};

    /**
     * @brief Extracts only the file basename (e.g., 'Server.cpp' from '/path/to/Server.cpp').
     */
    [[nodiscard]] std::string_view file_basename() const noexcept {
        std::string_view p = location.file_name();
        auto pos = p.find_last_of("/\\");
        if (pos != std::string_view::npos) {
            return p.substr(pos + 1);
        }
        return p;
    }
};

} // namespace aegon::log
