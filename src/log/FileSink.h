#pragma once

#include "log/Sink.h"
#include <fstream>
#include <mutex>
#include <string>
#include <filesystem>

namespace aegon::log {

struct FileSinkConfig {
    std::filesystem::path path{"server.log"};
    bool append{true};
    bool show_location{true};
    bool show_thread_id{false};
};

class FileSink : public ISink {
public:
    explicit FileSink(FileSinkConfig config);
    explicit FileSink(const std::filesystem::path& path, bool append = true);
    ~FileSink() override;

    void log(const Record& record) override;
    void flush() override;

    [[nodiscard]] bool is_open() const noexcept;

private:
    FileSinkConfig config_;
    std::ofstream file_;
    std::mutex mutex_;
};

} // namespace aegon::log
