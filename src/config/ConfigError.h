#pragma once

#include <string>
#include <sstream>

namespace aegon::config {

/**
 * @brief Represents configuration loading, parsing, or validation errors.
 */
struct ConfigError {
    std::string message{};
    std::string file_path{};
    size_t line{0};
    size_t column{0};

    [[nodiscard]] std::string to_string() const {
        std::ostringstream oss;
        if (!file_path.empty()) {
            oss << file_path;
            if (line > 0) {
                oss << ":" << line;
                if (column > 0) {
                    oss << ":" << column;
                }
            }
            oss << ": ";
        }
        oss << message;
        return oss.str();
    }
};

} // namespace aegon::config
