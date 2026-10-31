#pragma once

#include "storage/File.h"
#include "core/Task.h"

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <string_view>

namespace aegon::storage {

/**
 * @brief High-throughput crash-consistent Write-Ahead Log (WAL) with record framing and checksums.
 */
class WalLog {
public:
    WalLog() noexcept = default;
    explicit WalLog(File file, uint64_t last_seq = 0) noexcept;

    /**
     * @brief Opens or creates a WAL file.
     */
    static core::Task<WalLog> open(std::string_view path, StorageConfig config = {});

    /**
     * @brief Appends a record to the WAL, returning its monotonic sequence number.
     */
    core::Task<uint64_t> append(std::span<const std::byte> payload);

    /**
     * @brief Appends a string view record.
     */
    core::Task<uint64_t> append(std::string_view str) {
        return append(std::span<const std::byte>(reinterpret_cast<const std::byte*>(str.data()), str.size()));
    }

    /**
     * @brief Flushes pending WAL entries to disk.
     */
    core::Task<void> sync();

    /**
     * @brief Recovers all valid, uncorrupted records from the WAL upon startup.
     */
    core::Task<std::vector<std::vector<std::byte>>> recover();

    /**
     * @brief Closes the WAL file handle.
     */
    core::Task<void> close();

    [[nodiscard]] uint64_t last_seq() const noexcept { return last_seq_; }
    [[nodiscard]] bool is_valid() const noexcept { return file_.is_valid(); }

private:
    File file_;
    uint64_t last_seq_{0};
    int64_t append_offset_{0};
};

} // namespace aegon::storage

