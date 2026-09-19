#include "config/DotEnv.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>

namespace aegon::config {

namespace {

inline std::string_view trim_sv(std::string_view s) {
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::string parse_value(std::string_view val) {
    val = trim_sv(val);
    if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || 
                           (val.front() == '\'' && val.back() == '\''))) {
        char quote = val.front();
        val = val.substr(1, val.size() - 2);
        if (quote == '\'') {
            return std::string(val);
        }
        std::string result;
        result.reserve(val.size());
        for (size_t i = 0; i < val.size(); ++i) {
            if (val[i] == '\\' && i + 1 < val.size()) {
                char next = val[++i];
                switch (next) {
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case '\\': result += '\\'; break;
                    case '"': result += '"'; break;
                    default: result += next; break;
                }
            } else {
                result += val[i];
            }
        }
        return result;
    }
    // Unquoted value: strip trailing inline comments
    auto comment_pos = val.find('#');
    if (comment_pos != std::string_view::npos) {
        val = trim_sv(val.substr(0, comment_pos));
    }
    return std::string(val);
}

} // anonymous namespace

std::unordered_map<std::string, std::string> DotEnv::parse(std::string_view content) {
    std::unordered_map<std::string, std::string> result;
    size_t start = 0;

    while (start < content.size()) {
        size_t end = content.find('\n', start);
        std::string_view line = (end == std::string_view::npos)
                                    ? content.substr(start)
                                    : content.substr(start, end - start);
        start = (end == std::string_view::npos) ? content.size() : end + 1;

        line = trim_sv(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        // Strip leading "export "
        if (line.starts_with("export ") || line.starts_with("export\t")) {
            line = trim_sv(line.substr(7));
        }

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string_view::npos) {
            continue;
        }

        std::string_view key = trim_sv(line.substr(0, eq_pos));
        if (key.empty()) {
            continue;
        }

        std::string_view val = line.substr(eq_pos + 1);
        result[std::string(key)] = parse_value(val);
    }

    return result;
}

std::optional<std::unordered_map<std::string, std::string>> DotEnv::load_file(
    std::string_view file_path, bool set_environment) {
    std::ifstream file{std::string(file_path)};
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    auto env_map = parse(buffer.str());

    if (set_environment) {
        for (const auto& [k, v] : env_map) {
            ::setenv(k.c_str(), v.c_str(), 1);
        }
    }

    return env_map;
}

} // namespace aegon::config
