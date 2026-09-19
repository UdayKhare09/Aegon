#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

namespace aegon::config {

/**
 * @brief Parses and loads .env environment files.
 */
class DotEnv {
public:
    /**
     * @brief Parses .env format content into a key-value dictionary.
     */
    static std::unordered_map<std::string, std::string> parse(std::string_view content);

    /**
     * @brief Loads a .env file from disk.
     * @param file_path Path to the .env file.
     * @param set_environment If true, sets the parsed key-values into the process environment via setenv.
     */
    static std::optional<std::unordered_map<std::string, std::string>> load_file(
        std::string_view file_path, bool set_environment = false);
};

} // namespace aegon::config
