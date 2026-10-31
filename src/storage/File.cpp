#include "storage/File.h"
#include "core/EventLoop.h"
#include "core/engine/IoEngine.h"
#include "log/Logger.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>

namespace aegon::storage {

File::File(int fd, OpenMode mode, StorageConfig config, DeviceInfo dev_info, 
           bool is_direct, std::string path) noexcept
    : fd_(fd), mode_(mode), config_(std::move(config)), 
      device_info_(std::move(dev_info)), is_direct_(is_direct), path_(std::move(path)) {
    if (fd_ >= 0) {
        struct stat st{};
        if (::fstat(fd_, &st) == 0) {
            append_offset_ = st.st_size;
        }
    }
}

File::~File() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

File::File(File&& other) noexcept
    : fd_(other.fd_), mode_(other.mode_), config_(std::move(other.config_)),
      device_info_(std::move(other.device_info_)), is_direct_(other.is_direct_),
      path_(std::move(other.path_)), append_offset_(other.append_offset_) {
    other.fd_ = -1;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = other.fd_;
        mode_ = other.mode_;
        config_ = std::move(other.config_);
        device_info_ = std::move(other.device_info_);
        is_direct_ = other.is_direct_;
        path_ = std::move(other.path_);
        append_offset_ = other.append_offset_;
        other.fd_ = -1;
    }
    return *this;
}

core::Task<File> File::open(std::string_view path, OpenMode mode, StorageConfig config) {
    DeviceInfo dev = DeviceDetector::probe_path(path);

    int posix_flags = 0;
    if (has_flag(mode, OpenMode::ReadWrite)) {
        posix_flags |= O_RDWR;
    } else if (has_flag(mode, OpenMode::WriteOnly)) {
        posix_flags |= O_WRONLY;
    } else {
        posix_flags |= O_RDONLY;
    }

    if (has_flag(mode, OpenMode::Create))    posix_flags |= O_CREAT;
    if (has_flag(mode, OpenMode::Exclusive)) posix_flags |= O_EXCL;
    if (has_flag(mode, OpenMode::Truncate))  posix_flags |= O_TRUNC;
    if (has_flag(mode, OpenMode::Append))    posix_flags |= O_APPEND;
    if (has_flag(mode, OpenMode::Sync))      posix_flags |= O_SYNC;

#if defined(O_CLOEXEC)
    posix_flags |= O_CLOEXEC;
#endif

    bool try_direct = false;
    if (has_flag(mode, OpenMode::Direct) || config.direct_io == DirectIoMode::Always) {
        try_direct = true;
    } else if (config.direct_io == DirectIoMode::Auto) {
        if (dev.tier == DeviceTier::NVMe || dev.tier == DeviceTier::SATA_SSD) {
            try_direct = true;
        }
    }

    std::string path_str(path);
    int fd = -1;
    bool is_direct = false;

#if defined(O_DIRECT)
    if (try_direct) {
        fd = ::open(path_str.c_str(), posix_flags | O_DIRECT, 0644);
        if (fd >= 0) {
            is_direct = true;
        } else if (errno == EINVAL && config.direct_io != DirectIoMode::Always) {
            // Filesystem does not support O_DIRECT (e.g. tmpfs); fall back to buffered I/O
            fd = ::open(path_str.c_str(), posix_flags, 0644);
        }
    } else {
        fd = ::open(path_str.c_str(), posix_flags, 0644);
    }
#else
    fd = ::open(path_str.c_str(), posix_flags, 0644);
#endif

    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "Failed to open file: " + path_str);
    }

    if (dev.tier == DeviceTier::Unknown) {
        dev = DeviceDetector::probe_fd(fd);
    }

    co_return File(fd, mode, std::move(config), std::move(dev), is_direct, std::move(path_str));
}

core::Task<File> File::create(std::string_view path, StorageConfig config) {
    co_return co_await open(path, OpenMode::ReadWrite | OpenMode::Create | OpenMode::Truncate, std::move(config));
}

core::Task<size_t> File::read(int64_t offset, std::span<std::byte> buffer) {
    if (fd_ < 0) {
        throw std::runtime_error("Attempted read on closed File handle");
    }
    if (buffer.empty()) {
        co_return 0;
    }

    auto* loop = core::EventLoop::current();
    if (loop) {
        int res = co_await loop->engine().read(fd_, buffer.data(), buffer.size(), offset);
        if (res < 0) {
            throw std::system_error(-res, std::generic_category(), "File::read failed");
        }
        co_return static_cast<size_t>(res);
    }

    // Synchronous fallback
    ssize_t ret;
    if (offset >= 0) {
        ret = ::pread(fd_, buffer.data(), buffer.size(), offset);
    } else {
        ret = ::read(fd_, buffer.data(), buffer.size());
    }
    if (ret < 0) {
        throw std::system_error(errno, std::generic_category(), "File::read fallback failed");
    }
    co_return static_cast<size_t>(ret);
}

core::Task<size_t> File::write(int64_t offset, std::span<const std::byte> data) {
    if (fd_ < 0) {
        throw std::runtime_error("Attempted write on closed File handle");
    }
    if (data.empty()) {
        co_return 0;
    }

    auto* loop = core::EventLoop::current();
    if (loop) {
        int res = co_await loop->engine().write(fd_, data.data(), data.size(), offset);
        if (res < 0) {
            throw std::system_error(-res, std::generic_category(), "File::write failed");
        }
        if (offset >= 0 && offset + res > append_offset_) {
            append_offset_ = offset + res;
        }
        co_return static_cast<size_t>(res);
    }

    // Synchronous fallback
    ssize_t ret;
    if (offset >= 0) {
        ret = ::pwrite(fd_, data.data(), data.size(), offset);
    } else {
        ret = ::write(fd_, data.data(), data.size());
    }
    if (ret < 0) {
        throw std::system_error(errno, std::generic_category(), "File::write fallback failed");
    }
    if (offset >= 0 && offset + ret > append_offset_) {
        append_offset_ = offset + ret;
    }
    co_return static_cast<size_t>(ret);
}

core::Task<void> File::read_exact(int64_t offset, std::span<std::byte> buffer) {
    size_t total = 0;
    while (total < buffer.size()) {
        int64_t current_off = (offset >= 0) ? (offset + total) : -1;
        size_t n = co_await read(current_off, buffer.subspan(total));
        if (n == 0) {
            throw std::runtime_error("Unexpected EOF during File::read_exact");
        }
        total += n;
    }
}

core::Task<void> File::write_all(int64_t offset, std::span<const std::byte> data) {
    size_t total = 0;
    while (total < data.size()) {
        int64_t current_off = (offset >= 0) ? (offset + total) : -1;
        size_t n = co_await write(current_off, data.subspan(total));
        if (n == 0) {
            throw std::runtime_error("Zero bytes transferred during File::write_all");
        }
        total += n;
    }
}

core::Task<size_t> File::append(std::span<const std::byte> data) {
    size_t written = co_await write(append_offset_, data);
    append_offset_ += written;
    co_return written;
}

core::Task<void> File::sync(bool data_only) {
    if (fd_ < 0) co_return;

    auto* loop = core::EventLoop::current();
    if (loop) {
        int res = co_await loop->engine().fsync(fd_, data_only ? 1 : 0);
        if (res < 0) {
            throw std::system_error(-res, std::generic_category(), "File::sync failed");
        }
        co_return;
    }

    int ret = data_only ? ::fdatasync(fd_) : ::fsync(fd_);
    if (ret != 0) {
        throw std::system_error(errno, std::generic_category(), "File::sync fallback failed");
    }
}

core::Task<void> File::allocate(int64_t offset, int64_t len) {
    if (fd_ < 0) co_return;

    auto* loop = core::EventLoop::current();
    if (loop) {
        int res = co_await loop->engine().fallocate(fd_, 0, offset, len);
        if (res < 0) {
            throw std::system_error(-res, std::generic_category(), "File::allocate failed");
        }
        co_return;
    }

    int ret = ::fallocate(fd_, 0, offset, len);
    if (ret != 0) {
        throw std::system_error(errno, std::generic_category(), "File::allocate fallback failed");
    }
}

core::Task<void> File::truncate(int64_t new_size) {
    if (fd_ < 0) co_return;
    if (::ftruncate(fd_, new_size) != 0) {
        throw std::system_error(errno, std::generic_category(), "File::truncate failed");
    }
    append_offset_ = new_size;
    co_return;
}

core::Task<uint64_t> File::size() {
    if (fd_ < 0) co_return 0;
    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
        throw std::system_error(errno, std::generic_category(), "File::size failed");
    }
    co_return static_cast<uint64_t>(st.st_size);
}

core::Task<void> File::close() {
    if (fd_ >= 0) {
        int target_fd = fd_;
        fd_ = -1;

        auto* loop = core::EventLoop::current();
        if (loop) {
            (void)co_await loop->engine().close(target_fd);
        } else {
            ::close(target_fd);
        }
    }
}

} // namespace aegon::storage

