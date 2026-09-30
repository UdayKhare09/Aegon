#pragma once

#include "log/Record.h"
#include <memory>

namespace aegon::log {

/**
 * @brief Abstract destination for log records.
 */
class ISink {
public:
    virtual ~ISink() = default;

    /**
     * @brief Writes a log record to the destination.
     */
    virtual void log(const Record& record) = 0;

    /**
     * @brief Flushes any buffered output.
     */
    virtual void flush() = 0;
};

using SinkPtr = std::shared_ptr<ISink>;

} // namespace aegon::log
