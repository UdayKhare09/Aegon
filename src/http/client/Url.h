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
        u.scheme_offset_ = 0;
        u.scheme_len_ = scheme_end;

        // Skip "://"
        size_t authority_start = scheme_end + 3;
        if (authority_start >= u.raw_.size()) {
            return std::nullopt;
        }
        std::string_view auth_sv = std::string_view(u.raw_).substr(authority_start);

        // 2. Authority (host[:port])
        size_t rel_path_start = auth_sv.find_first_of("/?#");
        std::string_view authority = (rel_path_start == std::string_view::npos) ? auth_sv : auth_sv.substr(0, rel_path_start);

        if (authority.empty()) return std::nullopt;

        size_t colon_pos = authority.rfind(':');
        if (colon_pos != std::string_view::npos) {
            std::string_view host_sv = authority.substr(0, colon_pos);
            if (host_sv.empty()) return std::nullopt;
            u.host_offset_ = authority_start;
            u.host_len_ = colon_pos;

            std::string_view port_sv = authority.substr(colon_pos + 1);
            if (port_sv.empty()) return std::nullopt;
            uint16_t parsed_port = 0;
            auto [ptr, ec] = std::from_chars(port_sv.data(), port_sv.data() + port_sv.size(), parsed_port);
            if (ec != std::errc{} || ptr != port_sv.data() + port_sv.size() || parsed_port == 0) {
                return std::nullopt;
            }
            u.port_ = parsed_port;
            u.has_explicit_port_ = true;
        } else {
            u.host_offset_ = authority_start;
            u.host_len_ = authority.size();
            u.port_ = u.default_port_;
            u.has_explicit_port_ = false;
        }

        if (rel_path_start == std::string_view::npos) {
            u.has_custom_path_ = false;
            u.query_len_ = 0;
            u.fragment_len_ = 0;
            return u;
        }

        size_t path_start = authority_start + rel_path_start;
        std::string_view rem = std::string_view(u.raw_).substr(path_start);

        // 3. Path, Query, Fragment
        size_t fragment_pos = rem.find('#');
        if (fragment_pos != std::string_view::npos) {
            u.fragment_offset_ = path_start + fragment_pos + 1;
            u.fragment_len_ = rem.size() - (fragment_pos + 1);
            rem = rem.substr(0, fragment_pos);
        } else {
            u.fragment_len_ = 0;
        }

        size_t query_pos = rem.find('?');
        if (query_pos != std::string_view::npos) {
            u.query_offset_ = path_start + query_pos + 1;
            u.query_len_ = rem.size() - (query_pos + 1);
            rem = rem.substr(0, query_pos);
        } else {
            u.query_len_ = 0;
        }

        if (rem.empty()) {
            u.has_custom_path_ = false;
        } else {
            u.has_custom_path_ = true;
            u.path_offset_ = path_start;
            u.path_len_ = rem.size();
        }

        return u;
    }

    [[nodiscard]] std::string_view scheme() const noexcept {
        return std::string_view(raw_.data() + scheme_offset_, scheme_len_);
    }

    [[nodiscard]] std::string_view host() const noexcept {
        return std::string_view(raw_.data() + host_offset_, host_len_);
    }

    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] bool is_https() const noexcept { return is_https_; }
    [[nodiscard]] bool has_explicit_port() const noexcept { return has_explicit_port_; }

    [[nodiscard]] std::string_view path() const noexcept {
        if (!has_custom_path_) return "/";
        return std::string_view(raw_.data() + path_offset_, path_len_);
    }

    [[nodiscard]] std::string_view query() const noexcept {
        if (query_len_ == 0) return {};
        return std::string_view(raw_.data() + query_offset_, query_len_);
    }

    [[nodiscard]] std::string_view fragment() const noexcept {
        if (fragment_len_ == 0) return {};
        return std::string_view(raw_.data() + fragment_offset_, fragment_len_);
    }

    /**
     * @brief Formats the HTTP request-target (e.g. "/path?foo=bar").
     */
    [[nodiscard]] std::string target() const {
        auto q = query();
        auto p = path();
        if (q.empty()) return std::string(p);
        return std::string(p) + "?" + std::string(q);
    }

    /**
     * @brief Host header format (e.g. "api.example.com" or "localhost:8080" if non-standard port).
     */
    [[nodiscard]] std::string host_header() const {
        auto h = host();
        if (port_ == default_port_) {
            return std::string(h);
        }
        return std::string(h) + ":" + std::to_string(port_);
    }

    /**
     * @brief Unique cache key for connection pool (e.g. "https://api.example.com:443").
     */
    [[nodiscard]] std::string origin() const {
        auto h = host();
        return (is_https_ ? "https://" : "http://") + std::string(h) + ":" + std::to_string(port_);
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
            return std::string(scheme()) + ":" + std::string(location);
        }
        if (location.front() == '/') {
            return origin() + std::string(location);
        }
        // Relative path
        std::string p(path());
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
    size_t scheme_offset_{0};
    size_t scheme_len_{0};
    size_t host_offset_{0};
    size_t host_len_{0};
    uint16_t port_{80};
    uint16_t default_port_{80};
    bool is_https_{false};
    bool has_explicit_port_{false};
    size_t path_offset_{0};
    size_t path_len_{0};
    bool has_custom_path_{false};
    size_t query_offset_{0};
    size_t query_len_{0};
    size_t fragment_offset_{0};
    size_t fragment_len_{0};
};

} // namespace aegon::http::client
