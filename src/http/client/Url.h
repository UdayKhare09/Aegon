#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <cstdint>
#include <charconv>

namespace aegon::http::client {

/**
 * @brief Zero-copy URL view and parser for HTTP/HTTPS requests.
 */
class Url {
public:
    Url() = default;

    /**
     * @brief Parses an absolute URL string (e.g. "https://api.example.com:8443/v1/users?page=1#top").
     */
    static std::optional<Url> parse(std::string_view raw_url) {
        if (raw_url.empty()) return std::nullopt;

        Url u;
        u.raw_ = std::string(raw_url);
        std::string_view sv = u.raw_;

        // 1. Scheme
        size_t scheme_end = sv.find("://");
        if (scheme_end == std::string_view::npos) {
            return std::nullopt; // Absolute URL required
        }

        std::string_view scheme_sv = sv.substr(0, scheme_end);
        if (scheme_sv == "http" || scheme_sv == "HTTP") {
            u.is_https_ = false;
            u.default_port_ = 80;
        } else if (scheme_sv == "https" || scheme_sv == "HTTPS") {
            u.is_https_ = true;
            u.default_port_ = 443;
        } else {
            return std::nullopt; // Unsupported protocol
        }
        u.scheme_ = scheme_sv;

        // Skip "://"
        sv.remove_prefix(scheme_end + 3);

        // 2. Authority (host[:port])
        size_t path_start = sv.find_first_of("/?#");
        std::string_view authority = (path_start == std::string_view::npos) ? sv : sv.substr(0, path_start);

        if (authority.empty()) return std::nullopt;

        size_t colon_pos = authority.rfind(':');
        if (colon_pos != std::string_view::npos) {
            u.host_ = authority.substr(0, colon_pos);
            std::string_view port_sv = authority.substr(colon_pos + 1);
            uint16_t parsed_port = 0;
            auto [ptr, ec] = std::from_chars(port_sv.data(), port_sv.data() + port_sv.size(), parsed_port);
            if (ec != std::errc{} || parsed_port == 0) {
                return std::nullopt;
            }
            u.port_ = parsed_port;
            u.has_explicit_port_ = true;
        } else {
            u.host_ = authority;
            u.port_ = u.default_port_;
            u.has_explicit_port_ = false;
        }

        if (u.host_.empty()) return std::nullopt;

        if (path_start == std::string_view::npos) {
            u.path_ = "/";
            u.query_ = "";
            u.fragment_ = "";
            return u;
        }

        sv.remove_prefix(path_start);

        // 3. Path, Query, Fragment
        size_t fragment_pos = sv.find('#');
        if (fragment_pos != std::string_view::npos) {
            u.fragment_ = sv.substr(fragment_pos + 1);
            sv = sv.substr(0, fragment_pos);
        }

        size_t query_pos = sv.find('?');
        if (query_pos != std::string_view::npos) {
            u.query_ = sv.substr(query_pos + 1);
            u.path_ = sv.substr(0, query_pos);
        } else {
            u.path_ = sv;
            u.query_ = "";
        }

        if (u.path_.empty()) {
            u.path_ = "/";
        }

        return u;
    }

    [[nodiscard]] std::string_view scheme() const noexcept { return scheme_; }
    [[nodiscard]] std::string_view host() const noexcept { return host_; }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] bool is_https() const noexcept { return is_https_; }
    [[nodiscard]] bool has_explicit_port() const noexcept { return has_explicit_port_; }
    [[nodiscard]] std::string_view path() const noexcept { return path_; }
    [[nodiscard]] std::string_view query() const noexcept { return query_; }
    [[nodiscard]] std::string_view fragment() const noexcept { return fragment_; }

    /**
     * @brief Formats the HTTP request-target (e.g. "/path?foo=bar").
     */
    [[nodiscard]] std::string target() const {
        if (query_.empty()) return std::string(path_);
        return std::string(path_) + "?" + std::string(query_);
    }

    /**
     * @brief Host header format (e.g. "api.example.com" or "localhost:8080" if non-standard port).
     */
    [[nodiscard]] std::string host_header() const {
        if (port_ == default_port_) {
            return std::string(host_);
        }
        return std::string(host_) + ":" + std::to_string(port_);
    }

    /**
     * @brief Unique cache key for connection pool (e.g. "https://api.example.com:443").
     */
    [[nodiscard]] std::string origin() const {
        return (is_https_ ? "https://" : "http://") + std::string(host_) + ":" + std::to_string(port_);
    }

    /**
     * @brief Resolves a relative or absolute Location header URL against this base URL.
     */
    [[nodiscard]] std::string resolve(std::string_view location) const {
        if (location.empty()) return raw_;
        if (location.starts_with("http://") || location.starts_with("https://") ||
            location.starts_with("HTTP://") || location.starts_with("HTTPS://")) {
            return std::string(location);
        }
        if (location.starts_with("//")) {
            return std::string(scheme_) + ":" + std::string(location);
        }
        if (location.front() == '/') {
            return origin() + std::string(location);
        }
        // Relative path
        std::string p(path_);
        size_t last_slash = p.rfind('/');
        if (last_slash != std::string::npos) {
            p = p.substr(0, last_slash + 1);
        } else {
            p = "/";
        }
        return origin() + p + std::string(location);
    }

private:
    std::string raw_{};
    std::string_view scheme_{};
    std::string_view host_{};
    uint16_t port_{80};
    uint16_t default_port_{80};
    bool is_https_{false};
    bool has_explicit_port_{false};
    std::string_view path_{"/"};
    std::string_view query_{};
    std::string_view fragment_{};
};

} // namespace aegon::http::client
