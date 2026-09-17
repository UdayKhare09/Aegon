#pragma once

#include <stdexcept>
#include <string>

namespace aegon::data::orm::sql {

class OptimisticLockException : public std::runtime_error {
public:
    explicit OptimisticLockException(const std::string& message)
        : std::runtime_error(message) {}

    explicit OptimisticLockException(const char* message)
        : std::runtime_error(message) {}
};

} // namespace aegon::data::orm::sql
