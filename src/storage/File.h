#pragma once

#include "storage/StorageConfig.h"
#include "storage/DeviceDetector.h"
#include "storage/AlignedBuffer.h"
#include "core/Task.h"

#include <string>
#include <string_view>
#include <cstdint>
#include <cstddef>
#include <span>
#include <memory>
#include <system_error>

namespace aegon::storage {

/**
 * @brief High-performance asynchronous coroutine-based file handle.
 *
 * Supports zero-copy NVMe direct I/O (O_DIRECT), io_uring fixed submission,
 * and seamless fallback across epoll worker threads.
 */
class File {
public:
    File() noexcept = default;
    explicit File(int fd, OpenMode mode, StorageConfig config, DeviceInfo dev_info, 
                  bool is_direct, std::string path = "") noexcept;
    ~File();

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;

    // --- Asynchronous Factory Constructors ---

    /**
     * @brief Asynchronously opens an existing or new file.
     */
    static core::Task<File> open(std::string_view path, OpenMode mode = OpenMode::ReadOnly, 
                                 StorageConfig config = {});

    /**
     * @brief Creates or truncates a file for read/write.
     */
    static core::Task<File> create(std::string_view path, StorageConfig config = {});

    // --- Asynchronous Core I/O Operations ---

    /**
     * @brief Reads up to buffer.size() bytes at the given offset.
     * @return Number of bytes read (0 on EOF).
     */
    core::Task<size_t> read(int64_t offset, std::span<std::byte> buffer);

    /**
     * @brief Writes up to data.size() bytes at the given offset.
     * @return Number of bytes written.
     */
    core::Task<size_t> write(int64_t offset, std::span<const std::byte> data);

    /**
     * @brief Reads exactly buffer.size() bytes, looping until filled or EOF encountered.
     */
    core::Task<void> read_exact(int64_t offset, std::span<std::byte> buffer);

    /**
     * @brief Writes all bytes from data, looping until all are transferred.
     */
    core::Task<void> write_all(int64_t offset, std::span<const std::byte> data);

    /**
     * @brief Appends data to the end of the file atomically.
     */
    core::Task<size_t> append(std::span<const std::byte> data);

    /**
     * @brief Synchronously flushes cached file data to persistent physical media.
     * @param data_only If true, issues fdatasync (skipping metadata commit if safe).
     */
    core::Task<void> sync(bool data_only = true);

    /**
     * @brief Preallocates contiguous disk space via fallocate.
     */
    core::Task<void> allocate(int64_t offset, int64_t len);

    /**
     * @brief Truncates or extends file to specified length.
     */
    core::Task<void> truncate(int64_t new_size);

    /**
     * @brief Retrieves current physical file size in bytes.
     */
    core::Task<uint64_t> size();

    /**
     * @brief Asynchronously closes the file descriptor and releases kernel resources.
     */
    core::Task<void> close();

    // --- Accessors & State ---

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool is_valid() const noexcept { return fd_ >= 0; }
    [[nodiscard]] bool is_direct() const noexcept { return is_direct_; }
    [[nodiscard]] OpenMode mode() const noexcept { return mode_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] const DeviceInfo& device_info() const noexcept { return device_info_; }
    [[nodiscard]] const StorageConfig& config() const noexcept { return config_; }

private:
    int fd_{-1};
    OpenMode mode_{OpenMode::None};
    StorageConfig config_{};
    DeviceInfo device_info_{};
    bool is_direct_{false};
    std::string path_{};
    int64_t append_offset_{0};
};

} // namespace aegon::storage

