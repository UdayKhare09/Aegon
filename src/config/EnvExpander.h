#pragma once

#include "config/ConfigError.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <expected>

namespace aegon::config {

/**
 * @brief Expands ${VARIABLE} and ${VARIABLE:default} tokens in configuration strings.
 */
class EnvExpander {
public:
    /**
     * @brief Expands environment variable tokens within the input text.
     * @param text The input YAML content containing potential ${...} tokens.
     * @param overrides Optional dictionary of in-memory variables (e.g. from .env file).
     * @return The expanded string, or a ConfigError if a required variable without a default is missing.
     */
    static std::expected<std::string, ConfigError> expand(
        std::string_view text,
        const std::unordered_map<std::string, std::string>& overrides = {});
};

} // namespace aegon::config
