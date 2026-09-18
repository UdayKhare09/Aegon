#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <glaze/glaze.hpp>

namespace aegon::http {

/**
 * @brief Standard RFC 7807 Problem Details for HTTP APIs.
 *
 * Provides machine-readable format for specifying errors in HTTP API responses.
 */
struct ProblemDetails {
    std::string type{"about:blank"};
    std::string title;
    int status{500};
    std::string detail;
    std::string instance{};

    [[nodiscard]] std::string to_json() const {
        std::string out;
        std::ignore = glz::write_json(*this, out);
        return out;
    }
};

} // namespace aegon::http
