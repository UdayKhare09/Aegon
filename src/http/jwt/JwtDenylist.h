#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <chrono>
#include <concepts>

namespace aegon::http::jwt {

/**
 * @brief Abstract interface for token denylisting (e.g. replay prevention, logout revocation).
 */
class IJwtDenylist {
public:
    virtual ~IJwtDenylist() = default;

    /**
     * @brief Checks if a given JWT ID (jti) has been revoked.
     */
    [[nodiscard]] virtual bool is_revoked(std::string_view jti) const = 0;

    /**
     * @brief Adds a JWT ID (jti) to the revocation denylist until token expiration.
     */
    virtual void revoke(std::string_view jti, std::chrono::system_clock::time_point expires_at) = 0;
};

/**
 * @brief Concept verifying that a type satisfies the JwtDenylist requirements.
 */
template <typename T>
concept JwtDenylist = requires(T& d, const T& cd, std::string_view jti, std::chrono::system_clock::time_point exp) {
    { cd.is_revoked(jti) } -> std::convertible_to<bool>;
    { d.revoke(jti, exp) };
};

/**
 * @brief Thread-safe in-memory reference implementation of IJwtDenylist.
 */
class InMemoryJwtDenylist : public IJwtDenylist {
public:
    InMemoryJwtDenylist() = default;

    [[nodiscard]] bool is_revoked(std::string_view jti) const override {
        std::shared_lock lock(mtx_);
        auto it = entries_.find(std::string(jti));
        if (it == entries_.end()) {
            return false;
        }
        if (std::chrono::system_clock::now() > it->second) {
            return false; // Token TTL has already expired naturally
        }
        return true;
    }

    void revoke(std::string_view jti, std::chrono::system_clock::time_point expires_at) override {
        std::unique_lock lock(mtx_);
        entries_[std::string(jti)] = expires_at;
    }

    /**
     * @brief Cleans up expired denylist entries to free memory.
     */
    void prune() {
        std::unique_lock lock(mtx_);
        auto now = std::chrono::system_clock::now();
        std::erase_if(entries_, [now](const auto& item) {
            return now > item.second;
        });
    }

    /**
     * @brief Returns the count of actively tracked denylisted tokens.
     */
    [[nodiscard]] size_t size() const {
        std::shared_lock lock(mtx_);
        return entries_.size();
    }

    /**
     * @brief Clears all denylisted tokens.
     */
    void clear() {
        std::unique_lock lock(mtx_);
        entries_.clear();
    }

private:
    mutable std::shared_mutex mtx_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> entries_;
};

} // namespace aegon::http::jwt
