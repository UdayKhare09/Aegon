#pragma once

#include "config/ConfigError.h"
#include "config/EnvExpander.h"
#include "config/DotEnv.h"
#include <glaze/yaml.hpp>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <expected>
#include <cctype>
#include <unistd.h>

extern char** environ;

namespace aegon::config {

namespace detail {

inline std::string sanitize_yaml_value(std::string_view val) {
    if (val == "true" || val == "false" || val == "null" || val == "~") {
        return std::string(val);
    }
    // Check if pure integer or float
    if (!val.empty()) {
        bool is_num = true;
        bool has_dot = false;
        size_t start = (val.front() == '-' || val.front() == '+') ? 1 : 0;
        if (start < val.size()) {
            for (size_t i = start; i < val.size(); ++i) {
                if (val[i] == '.' && !has_dot) {
                    has_dot = true;
                } else if (!std::isdigit(static_cast<unsigned char>(val[i]))) {
                    is_num = false;
                    break;
                }
            }
            if (is_num) {
                return std::string(val);
            }
        }
    }
    // Quote string
    std::string q = "\"";
    for (char c : val) {
        if (c == '"') q += "\\\"";
        else if (c == '\\') q += "\\\\";
        else if (c == '\n') q += "\\n";
        else if (c == '\r') q += "\\r";
        else if (c == '\t') q += "\\t";
        else q += c;
    }
    q += "\"";
    return q;
}

inline std::string path_segments_to_yaml(const std::vector<std::string>& segments, std::string_view val) {
    if (segments.empty()) return "";
    std::string yaml;
    for (size_t i = 0; i < segments.size(); ++i) {
        yaml.append(i * 2, ' ');
        yaml += segments[i];
        if (i + 1 == segments.size()) {
            yaml += ": ";
            yaml += sanitize_yaml_value(val);
            yaml += "\n";
        } else {
            yaml += ":\n";
        }
    }
    return yaml;
}

inline std::vector<std::string> split_env_key(std::string_view key) {
    std::vector<std::string> segments;
    size_t start = 0;
    while (start < key.size()) {
        size_t pos = key.find("__", start);
        std::string_view seg = (pos == std::string_view::npos) ? key.substr(start) : key.substr(start, pos - start);
        if (!seg.empty()) {
            std::string s;
            s.reserve(seg.size());
            for (char c : seg) {
                s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            segments.push_back(std::move(s));
        }
        if (pos == std::string_view::npos) break;
        start = pos + 2;
    }
    return segments;
}

inline std::vector<std::string> split_cli_key(std::string_view key) {
    std::vector<std::string> segments;
    size_t start = 0;
    while (start < key.size()) {
        size_t dot_pos = key.find('.', start);
        size_t dunder_pos = key.find("__", start);
        size_t pos = std::min(dot_pos, dunder_pos);
        if (pos == std::string_view::npos) {
            std::string_view seg = key.substr(start);
            if (!seg.empty()) {
                std::string s;
                s.reserve(seg.size());
                for (char c : seg) {
                    s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                segments.push_back(std::move(s));
            }
            break;
        }
        std::string_view seg = key.substr(start, pos - start);
        if (!seg.empty()) {
            std::string s;
            s.reserve(seg.size());
            for (char c : seg) {
                s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            segments.push_back(std::move(s));
        }
        if (pos == dot_pos) {
            start = pos + 1;
        } else {
            start = pos + 2;
        }
    }
    return segments;
}

} // namespace detail

/**
 * @brief Fluent, layered configuration builder supporting YAML 1.2,
 * .env files, ${VAR:default} expansion, environment prefixes, and CLI arguments.
 */
template <typename T>
class ConfigBuilder {
public:
    ConfigBuilder() = default;

    /**
     * @brief Adds a YAML configuration file from disk.
     * @param path Path to .yaml or .yml file.
     * @param optional If true, missing files are ignored; if false, reports error.
     */
    ConfigBuilder& add_yaml_file(std::string_view path, bool optional = false) {
        std::ifstream file{std::string(path)};
        if (!file.is_open()) {
            if (!optional) {
                missing_files_.push_back(std::string(path));
            }
            return *this;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        yaml_sources_.push_back({std::string(path), buffer.str()});
        return *this;
    }

    /// Alias for add_yaml_file
    ConfigBuilder& add_file(std::string_view path, bool optional = false) {
        return add_yaml_file(path, optional);
    }

    /**
     * @brief Adds in-memory YAML content.
     */
    ConfigBuilder& add_yaml_content(std::string_view yaml_str, std::string_view source_name = "<inline_yaml>") {
        yaml_sources_.push_back({std::string(source_name), std::string(yaml_str)});
        return *this;
    }

    /// Alias for add_yaml_content
    ConfigBuilder& add_string(std::string_view yaml_str, std::string_view source_name = "<inline_yaml>") {
        return add_yaml_content(yaml_str, source_name);
    }

    /**
     * @brief Loads and applies a .env file to the variable resolution table.
     * @param path Path to .env file (default: ".env").
     * @param optional If true, silently ignores missing .env file.
     */
    ConfigBuilder& add_env_file(std::string_view path = ".env", bool optional = true) {
        auto loaded = DotEnv::load_file(path, /*set_environment=*/false);
        if (loaded) {
            for (auto& [k, v] : *loaded) {
                env_overrides_[k] = std::move(v);
            }
        } else if (!optional) {
            missing_files_.push_back(std::string(path));
        }
        return *this;
    }

    /// Alias for add_env_file
    ConfigBuilder& add_dotenv_file(std::string_view path = ".env", bool optional = true) {
        return add_env_file(path, optional);
    }

    /**
     * @brief Adds a manual in-memory environment variable override.
     */
    ConfigBuilder& add_env(std::string key, std::string value) {
        env_overrides_[std::move(key)] = std::move(value);
        return *this;
    }

    /**
     * @brief Scans system environment variables with the specified prefix (e.g. "AEGON_"),
     * mapping e.g. "AEGON__SERVER__PORT=9090" to YAML structure "server: port: 9090".
     */
    ConfigBuilder& add_env_prefix(std::string_view prefix) {
        env_prefixes_.push_back(std::string(prefix));
        return *this;
    }

    /**
     * @brief Overrides configuration from command-line arguments (e.g. "--server.port=9090").
     */
    ConfigBuilder& add_cli_args(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (!arg.starts_with("--")) continue;
            arg.remove_prefix(2);

            size_t eq_pos = arg.find('=');
            if (eq_pos == std::string_view::npos) continue;

            std::string_view key = arg.substr(0, eq_pos);
            std::string_view val = arg.substr(eq_pos + 1);

            auto segments = detail::split_cli_key(key);
            if (!segments.empty()) {
                cli_snippets_.push_back(detail::path_segments_to_yaml(segments, val));
            }
        }
        return *this;
    }

    ConfigBuilder& add_cli_args(const std::vector<std::string>& args) {
        for (const auto& raw_arg : args) {
            std::string_view arg = raw_arg;
            if (!arg.starts_with("--")) continue;
            arg.remove_prefix(2);

            size_t eq_pos = arg.find('=');
            if (eq_pos == std::string_view::npos) continue;

            std::string_view key = arg.substr(0, eq_pos);
            std::string_view val = arg.substr(eq_pos + 1);

            auto segments = detail::split_cli_key(key);
            if (!segments.empty()) {
                cli_snippets_.push_back(detail::path_segments_to_yaml(segments, val));
            }
        }
        return *this;
    }

    /// Alias for add_cli_args
    ConfigBuilder& add_cli_flags(int argc, char* argv[]) {
        return add_cli_args(argc, argv);
    }

    ConfigBuilder& add_cli_flags(const std::vector<std::string>& args) {
        return add_cli_args(args);
    }

    /**
     * @brief Enables or disables ${VARIABLE:default} expansion in YAML content.
     */
    ConfigBuilder& expand_env_vars(bool enable = true) {
        expand_env_vars_ = enable;
        return *this;
    }

    /**
     * @brief Builds and validates the configuration into typed struct T.
     */
    std::expected<T, ConfigError> build() {
        // 1. Check missing mandatory files
        if (!missing_files_.empty()) {
            ConfigError err;
            err.file_path = missing_files_.front();
            err.message = "Mandatory configuration file not found: " + missing_files_.front();
            return std::unexpected(err);
        }

        T result{};

        // 2. Apply configured YAML files / contents in layered order
        for (const auto& [source_path, raw_content] : yaml_sources_) {
            std::string yaml_to_parse = raw_content;
            if (expand_env_vars_) {
                auto expanded = EnvExpander::expand(raw_content, env_overrides_);
                if (!expanded) {
                    ConfigError err = expanded.error();
                    err.file_path = source_path;
                    return std::unexpected(err);
                }
                yaml_to_parse = std::move(*expanded);
            }

            auto ec = glz::read_yaml(result, yaml_to_parse);
            if (ec) {
                ConfigError err;
                err.file_path = source_path;
                err.message = glz::format_error(ec, yaml_to_parse);
                return std::unexpected(err);
            }
        }

        // 3. Apply Environment Variable Prefix Overrides
        if (!env_prefixes_.empty()) {
            std::vector<std::string> env_snippets;
            auto process_kv = [&](std::string_view k, std::string_view v) {
                for (const auto& prefix : env_prefixes_) {
                    if (k.starts_with(prefix)) {
                        std::string_view remainder = k.substr(prefix.size());
                        while (remainder.starts_with('_')) remainder.remove_prefix(1);
                        auto segments = detail::split_env_key(remainder);
                        if (!segments.empty()) {
                            env_snippets.push_back(detail::path_segments_to_yaml(segments, v));
                        }
                        break;
                    }
                }
            };

            if (environ != nullptr) {
                for (char** env = environ; *env != nullptr; ++env) {
                    std::string_view entry = *env;
                    size_t eq = entry.find('=');
                    if (eq == std::string_view::npos) continue;
                    process_kv(entry.substr(0, eq), entry.substr(eq + 1));
                }
            }

            for (const auto& [k, v] : env_overrides_) {
                process_kv(k, v);
            }

            for (const auto& snippet : env_snippets) {
                auto ec = glz::read_yaml(result, snippet);
                if (ec) {
                    ConfigError err;
                    err.file_path = "<environment_variables>";
                    err.message = glz::format_error(ec, snippet);
                    return std::unexpected(err);
                }
            }
        }

        // 4. Apply CLI Flag Overrides
        for (const auto& snippet : cli_snippets_) {
            auto ec = glz::read_yaml(result, snippet);
            if (ec) {
                ConfigError err;
                err.file_path = "<command_line_flags>";
                err.message = glz::format_error(ec, snippet);
                return std::unexpected(err);
            }
        }

        return result;
    }

private:
    struct YamlSource {
        std::string name;
        std::string content;
    };

    std::vector<YamlSource> yaml_sources_;
    std::vector<std::string> missing_files_;
    std::unordered_map<std::string, std::string> env_overrides_;
    std::vector<std::string> env_prefixes_;
    std::vector<std::string> cli_snippets_;
    bool expand_env_vars_{true};
};

} // namespace aegon::config
