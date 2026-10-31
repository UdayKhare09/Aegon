#include "storage/FileStream.h"
#include <algorithm>
#include <cstring>

namespace aegon::storage {

FileReadStream::FileReadStream(File& file, size_t chunk_size, int64_t start_offset, int64_t max_bytes)
    : file_(file),
      chunk_size_(chunk_size > 0 ? chunk_size : file.config().default_chunk_size),
      current_offset_(start_offset >= 0 ? start_offset : 0),
      remaining_bytes_(max_bytes),
      buffer_(chunk_size_ + (file.is_direct() ? 8192 : 0), 
              file.device_info().physical_block_size > 0 ? file.device_info().physical_block_size : 4096) {}

core::Task<std::optional<std::span<const std::byte>>> FileReadStream::next_chunk() {
    if (eof_ || remaining_bytes_ == 0) {
        co_return std::nullopt;
    }

    size_t to_read = chunk_size_;
    if (remaining_bytes_ > 0 && static_cast<int64_t>(to_read) > remaining_bytes_) {
        to_read = static_cast<size_t>(remaining_bytes_);
    }

    if (file_.is_direct()) {
        size_t block_size = file_.device_info().physical_block_size > 0 ? 
                            file_.device_info().physical_block_size : 4096;

        int64_t aligned_offset = (current_offset_ / block_size) * block_size;
        size_t offset_in_block = static_cast<size_t>(current_offset_ - aligned_offset);
        size_t raw_len = ((offset_in_block + to_read + block_size - 1) / block_size) * block_size;

        if (raw_len > buffer_.capacity()) {
            buffer_.allocate(raw_len);
        }

        size_t n = co_await file_.read(aligned_offset, buffer_.full_capacity_span().subspan(0, raw_len));
        if (n <= offset_in_block) {
            eof_ = true;
            co_return std::nullopt;
        }

        size_t valid_bytes = n - offset_in_block;
        size_t result_len = std::min(to_read, valid_bytes);

        current_offset_ += result_len;
        total_read_ += result_len;
        if (remaining_bytes_ > 0) remaining_bytes_ -= result_len;

        if (n < raw_len) {
            eof_ = true;
        }

        co_return std::span<const std::byte>(buffer_.byte_data() + offset_in_block, result_len);
    }

    // Standard buffered read
    size_t n = co_await file_.read(current_offset_, buffer_.full_capacity_span().subspan(0, to_read));
    if (n == 0) {
        eof_ = true;
        co_return std::nullopt;
    }

    current_offset_ += n;
    total_read_ += n;
    if (remaining_bytes_ > 0) remaining_bytes_ -= n;

    if (n < to_read) {
        eof_ = true;
    }

    co_return std::span<const std::byte>(buffer_.byte_data(), n);
}

FileWriteStream::FileWriteStream(File& file, size_t chunk_size, int64_t start_offset)
    : file_(file),
      chunk_size_(chunk_size > 0 ? chunk_size : file.config().default_chunk_size),
      current_offset_(start_offset >= 0 ? start_offset : 0),
      buffer_(chunk_size_, file.device_info().physical_block_size > 0 ? 
                           file.device_info().physical_block_size : 4096) {}

FileWriteStream::~FileWriteStream() {
    // In synchronous destructor we cannot co_await, but buffered writes should be explicitly flushed
}

core::Task<void> FileWriteStream::write_chunk(std::span<const std::byte> chunk) {
    size_t offset = 0;
    while (offset < chunk.size()) {
        size_t space = chunk_size_ - buffered_bytes_;
        size_t take = std::min(space, chunk.size() - offset);

        std::memcpy(buffer_.byte_data() + buffered_bytes_, chunk.data() + offset, take);
        buffered_bytes_ += take;
        offset += take;

        if (buffered_bytes_ >= chunk_size_) {
            co_await flush();
        }
    }
}

core::Task<void> FileWriteStream::flush() {
    if (buffered_bytes_ == 0) co_return;

    co_await file_.write_all(current_offset_, std::span<const std::byte>(buffer_.byte_data(), buffered_bytes_));
    current_offset_ += buffered_bytes_;
    total_written_ += buffered_bytes_;
    buffered_bytes_ = 0;
}

} // namespace aegon::storage

