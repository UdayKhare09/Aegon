#include "config/EnvExpander.h"
#include <cstdlib>

namespace aegon::config {

std::expected<std::string, ConfigError> EnvExpander::expand(
    std::string_view text,
    const std::unordered_map<std::string, std::string>& overrides) {
    std::string result;
    result.reserve(text.size());

    size_t i = 0;
    size_t current_line = 1;
    size_t line_start = 0;

    while (i < text.size()) {
        if (text[i] == '\n') {
            current_line++;
            line_start = i + 1;
            result += text[i++];
            continue;
        }

        // Check for escaped \${VAR}
        if (text[i] == '\\' && i + 2 < text.size() && text[i + 1] == '$' && text[i + 2] == '{') {
            result += "${";
            i += 3;
            continue;
        }

        // Check for ${VAR} or ${VAR:default}
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') {
            size_t token_start_col = i - line_start + 1;
            size_t close_pos = text.find('}', i + 2);
            if (close_pos == std::string_view::npos) {
                // Unterminated ${ token
                ConfigError err;
                err.message = "Unterminated environment variable expansion token '${'";
                err.line = current_line;
                err.column = token_start_col;
                return std::unexpected(err);
            }

            std::string_view token = text.substr(i + 2, close_pos - (i + 2));
            i = close_pos + 1;

            std::string_view var_name = token;
            std::optional<std::string_view> default_val;

            size_t colon_pos = token.find(':');
            if (colon_pos != std::string_view::npos) {
                var_name = token.substr(0, colon_pos);
                default_val = token.substr(colon_pos + 1);
            }

            // Trim any whitespace around variable name
            while (!var_name.empty() && var_name.front() == ' ') var_name.remove_prefix(1);
            while (!var_name.empty() && var_name.back() == ' ') var_name.remove_suffix(1);

            if (var_name.empty()) {
                ConfigError err;
                err.message = "Empty environment variable name inside '${}'";
                err.line = current_line;
                err.column = token_start_col;
                return std::unexpected(err);
            }

            // 1. Check in-memory overrides (.env or builder dictionary)
            std::string var_name_str(var_name);
            auto it = overrides.find(var_name_str);
            if (it != overrides.end()) {
                result += it->second;
                continue;
            }

            // 2. Check process environment (std::getenv)
            const char* env_val = std::getenv(var_name_str.c_str());
            if (env_val != nullptr) {
                result += env_val;
                continue;
            }

            // 3. Check if a default value was provided
            if (default_val.has_value()) {
                result += *default_val;
                continue;
            }

            // 4. Missing required environment variable
            ConfigError err;
            err.message = "Required environment variable '" + var_name_str + "' is not set and has no default value";
            err.line = current_line;
            err.column = token_start_col;
            return std::unexpected(err);
        }

        result += text[i++];
    }

    return result;
}

} // namespace aegon::config
