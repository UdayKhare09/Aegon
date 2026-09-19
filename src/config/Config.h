#pragma once

#include "config/ConfigBuilder.h"
#include "config/ConfigError.h"
#include "config/DotEnv.h"
#include "config/EnvExpander.h"

namespace aegon::config {

/**
 * @brief High-level facade for loading configuration in Aegon.
 */
class Config {
public:
    /**
     * @brief Create a fluent ConfigBuilder for type T.
     */
    template <typename T>
    static ConfigBuilder<T> builder() {
        return ConfigBuilder<T>{};
    }

    /**
     * @brief Convenience shortcut to load a single YAML file into struct T.
     * Environment variable expansion (${VAR:default}) is performed automatically.
     */
    template <typename T>
    static std::expected<T, ConfigError> load(const std::string& file_path) {
        return ConfigBuilder<T>{}
            .add_file(file_path)
            .build();
    }

    /**
     * @brief Convenience shortcut to parse in-memory YAML content into struct T.
     */
    template <typename T>
    static std::expected<T, ConfigError> load_string(std::string_view yaml_content, std::string_view source_name = "<memory>") {
        return ConfigBuilder<T>{}
            .add_string(yaml_content, source_name)
            .build();
    }
};

} // namespace aegon::config
