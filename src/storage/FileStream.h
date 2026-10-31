#pragma once

#include "storage/File.h"
#include "storage/AlignedBuffer.h"
#include "core/Task.h"

#include <optional>
#include <span>
#include <cstdint>
#include <cstddef>

namespace aegon::storage {

/**
 * @brief High-throughput streaming async file reader with sector-aligned Direct I/O handling.
 */
class FileReadStream {
public:
    explicit FileReadStream(File& file, size_t chunk_size = 64 * 1024, 
                            int64_t start_offset = 0, int64_t max_bytes = -1);

    /**
     * @brief Fetches the next contiguous chunk of data from the file.
     * @return Span of read bytes, or std::nullopt when EOF is reached.
     */
    core::Task<std::optional<std::span<const std::byte>>> next_chunk();

    [[nodiscard]] int64_t position() const noexcept { return current_offset_; }
    [[nodiscard]] int64_t total_read() const noexcept { return total_read_; }
    [[nodiscard]] bool is_eof() const noexcept { return eof_; }
    [[nodiscard]] size_t chunk_size() const noexcept { return chunk_size_; }

private:
    File& file_;
    size_t chunk_size_{64 * 1024};
    int64_t current_offset_{0};
    int64_t remaining_bytes_{-1};
    int64_t total_read_{0};
    bool eof_{false};
    AlignedBuffer buffer_;
};

/**
 * @brief High-throughput buffered streaming async file writer.
 */
class FileWriteStream {
public:
    explicit FileWriteStream(File& file, size_t chunk_size = 64 * 1024, 
                             int64_t start_offset = 0);
    ~FileWriteStream();

    /**
     * @brief Writes a chunk into the stream, flushing when buffer fills.
     */
    core::Task<void> write_chunk(std::span<const std::byte> chunk);

    /**
     * @brief Flushes any buffered bytes to disk.
     */
    core::Task<void> flush();

    [[nodiscard]] int64_t position() const noexcept { return current_offset_; }
    [[nodiscard]] int64_t total_written() const noexcept { return total_written_; }

private:
    File& file_;
    size_t chunk_size_{64 * 1024};
    int64_t current_offset_{0};
    int64_t total_written_{0};
    AlignedBuffer buffer_;
    size_t buffered_bytes_{0};
};

} // namespace aegon::storage

