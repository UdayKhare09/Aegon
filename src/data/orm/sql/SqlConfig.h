#pragma once

#include "Dialect.h"
#include <string>
#include <cstdint>
#include <cstddef>

namespace aegon::data::orm::sql {

/**
 * @brief Unified configuration for SQL database engines (PostgreSQL, SQLite, MySQL).
 *
 * Supports designated-initializer configuration on Server:
 * @code
 * server.db.sql({
 *     .dialect = DatabaseDialect::PostgreSQL,
 *     .host = "127.0.0.1",
 *     .port = 5432,
 *     .database = "mydb",
 *     .user = "postgres",
 *     .password = "secret",
 *     .pool_per_core = 4
 * });
 * @endcode
 */
struct SqlConfig {
    DatabaseDialect dialect{DatabaseDialect::SQLite};
    std::string host{"127.0.0.1"};
    uint16_t port{5432};
    std::string database;
    std::string user;
    std::string password;
    std::string url;
    size_t pool_per_core{4};

    [[nodiscard]] std::string to_conninfo() const {
        if (!url.empty()) {
            return url;
        }

        std::string info;
        info.reserve(128);

        if (!host.empty()) {
            info.append("host=").append(host).push_back(' ');
        }
        if (port != 0) {
            info.append("port=").append(std::to_string(port)).push_back(' ');
        }
        if (!database.empty()) {
            info.append("dbname=").append(database).push_back(' ');
        }
        if (!user.empty()) {
            info.append("user=").append(user).push_back(' ');
        }
        if (!password.empty()) {
            info.append("password=").append(password).push_back(' ');
        }

        if (!info.empty() && info.back() == ' ') {
            info.pop_back();
        }
        return info;
    }
};

} // namespace aegon::data::orm::sql
