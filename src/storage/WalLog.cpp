#include "storage/WalLog.h"
#include <cstring>
#include <array>

namespace aegon::storage {

namespace {

constexpr uint32_t WAL_MAGIC = 0xAE601060;

#pragma pack(push, 1)
struct WalRecordHeader {
    uint32_t magic{WAL_MAGIC};
    uint64_t seq{0};
    uint32_t length{0};
    uint32_t crc32{0};
};
#pragma pack(pop)

static uint32_t calculate_crc32(const void* data, size_t length) noexcept {
    static const auto table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (size_t j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    const auto* buf = static_cast<const uint8_t*>(data);
    uint32_t c = 0xFFFFFFFFL;
    for (size_t i = 0; i < length; ++i) {
        c = table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFL;
}

} // namespace

WalLog::WalLog(File file, uint64_t last_seq) noexcept
    : file_(std::move(file)), last_seq_(last_seq) {}

core::Task<WalLog> WalLog::open(std::string_view path, StorageConfig config) {
    File file = co_await File::open(path, OpenMode::ReadWrite | OpenMode::Create, std::move(config));
    uint64_t file_sz = co_await file.size();

    WalLog wal(std::move(file), 0);
    wal.append_offset_ = static_cast<int64_t>(file_sz);

    if (file_sz > 0) {
        // Recover to determine last sequence number and valid append offset
        auto records = co_await wal.recover();
        (void)records;
    }

    co_return wal;
}

core::Task<uint64_t> WalLog::append(std::span<const std::byte> payload) {
    uint64_t seq = ++last_seq_;
    WalRecordHeader hdr{
        .magic = WAL_MAGIC,
        .seq = seq,
        .length = static_cast<uint32_t>(payload.size()),
        .crc32 = calculate_crc32(payload.data(), payload.size())
    };

    std::vector<std::byte> buffer(sizeof(WalRecordHeader) + payload.size());
    std::memcpy(buffer.data(), &hdr, sizeof(WalRecordHeader));
    if (!payload.empty()) {
        std::memcpy(buffer.data() + sizeof(WalRecordHeader), payload.data(), payload.size());
    }

    co_await file_.write_all(append_offset_, buffer);
    append_offset_ += static_cast<int64_t>(buffer.size());
    co_return seq;
}

core::Task<void> WalLog::sync() {
    co_await file_.sync(true);
}

core::Task<std::vector<std::vector<std::byte>>> WalLog::recover() {
    std::vector<std::vector<std::byte>> records;
    uint64_t file_sz = co_await file_.size();
    int64_t offset = 0;
    uint64_t max_seq = 0;

    while (static_cast<uint64_t>(offset) + sizeof(WalRecordHeader) <= file_sz) {
        WalRecordHeader hdr{};
        try {
            co_await file_.read_exact(offset, std::span<std::byte>(reinterpret_cast<std::byte*>(&hdr), sizeof(hdr)));
        } catch (...) {
            break;
        }

        if (hdr.magic != WAL_MAGIC) {
            break; // Header corruption or unwritten block
        }

        if (static_cast<uint64_t>(offset) + sizeof(WalRecordHeader) + hdr.length > file_sz) {
            break; // Incomplete payload write on crash
        }

        std::vector<std::byte> payload(hdr.length);
        if (hdr.length > 0) {
            try {
                co_await file_.read_exact(offset + sizeof(WalRecordHeader), payload);
            } catch (...) {
                break;
            }

            uint32_t check = calculate_crc32(payload.data(), payload.size());
            if (check != hdr.crc32) {
                break; // Payload corrupted
            }
        }

        if (hdr.seq > max_seq) {
            max_seq = hdr.seq;
        }

        records.push_back(std::move(payload));
        offset += sizeof(WalRecordHeader) + hdr.length;
    }

    last_seq_ = max_seq;
    append_offset_ = offset;
    co_return records;
}

core::Task<void> WalLog::close() {
    co_await file_.close();
}

} // namespace aegon::storage

