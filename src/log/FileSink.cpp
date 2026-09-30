#include "log/FileSink.h"
#include <format>
#include <ctime>

namespace aegon::log {

namespace {

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
    auto tp_millis = std::chrono::floor<std::chrono::milliseconds>(tp);
    auto secs = std::chrono::floor<std::chrono::seconds>(tp_millis);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(tp_millis - secs).count();

    std::time_t t = std::chrono::system_clock::to_time_t(secs);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
                       tm_buf.tm_year + 1900,
                       tm_buf.tm_mon + 1,
                       tm_buf.tm_mday,
                       tm_buf.tm_hour,
                       tm_buf.tm_min,
                       tm_buf.tm_sec,
                       millis);
}

} // namespace

FileSink::FileSink(FileSinkConfig config)
    : config_(std::move(config)) {
    if (config_.path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(config_.path.parent_path(), ec);
    }
    auto mode = std::ios::out | (config_.append ? std::ios::app : std::ios::trunc);
    file_.open(config_.path, mode);
}

FileSink::FileSink(const std::filesystem::path& path, bool append)
    : FileSink(FileSinkConfig{.path = path, .append = append}) {}

FileSink::~FileSink() {
    flush();
    if (file_.is_open()) {
        file_.close();
    }
}

bool FileSink::is_open() const noexcept {
    return file_.is_open();
}

void FileSink::log(const Record& record) {
    if (!file_.is_open()) return;

    std::string ts = format_timestamp(record.timestamp);
    std::string_view lvl_str = to_string(record.level);

    std::string buffer;
    buffer.reserve(256 + record.message.size());

    std::format_to(std::back_inserter(buffer), "[{}] [{:5}] ", ts, lvl_str);

    if (config_.show_thread_id) {
        std::format_to(std::back_inserter(buffer), "[t:{:x}] ", record.thread_id);
    }

    if (config_.show_location && record.location.line() > 0) {
        std::format_to(std::back_inserter(buffer), "[{}:{}] ",
                       record.file_basename(), record.location.line());
    }

    buffer.append(record.message);
    buffer.push_back('\n');

    std::lock_guard<std::mutex> lock(mutex_);
    file_.write(buffer.data(), buffer.size());
}

void FileSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
    }
}

} // namespace aegon::log
