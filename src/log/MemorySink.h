#pragma once

#include "log/Sink.h"
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>

namespace aegon::log {

/**
 * @brief In-memory sink primarily used for unit testing and log assertions.
 */
class MemorySink : public ISink {
public:
    MemorySink() = default;
    ~MemorySink() override = default;

    void log(const Record& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.push_back(record);
        messages_.push_back(record.message);
    }

    void flush() override {}

    [[nodiscard]] std::vector<Record> records() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_;
    }

    [[nodiscard]] std::vector<std::string> messages() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_.size();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_.empty();
    }

    [[nodiscard]] bool contains(std::string_view substr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::any_of(messages_.begin(), messages_.end(), [substr](const std::string& msg) {
            return msg.find(substr) != std::string::npos;
        });
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();
        messages_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::vector<Record> records_;
    std::vector<std::string> messages_;
};

} // namespace aegon::log
