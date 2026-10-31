#include "storage/StorageConfig.h"
#include "storage/DeviceDetector.h"
#include "storage/AlignedBuffer.h"
#include "storage/File.h"
#include "storage/FileStream.h"
#include "storage/WalLog.h"
#include "core/EventLoop.h"
#include "core/Task.h"

#include <iostream>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace aegon;
using namespace aegon::storage;

void test_device_detector() {
    std::cout << "--- [Test 1] DeviceDetector ---\n";
    auto info = DeviceDetector::probe_path(".");
    std::cout << "  Device name: " << info.device_name << "\n";
    std::cout << "  Device tier: " << static_cast<int>(info.tier) << "\n";
    std::cout << "  Rotational: " << (info.rotational ? "yes" : "no") << "\n";
    std::cout << "  Logical block size: " << info.logical_block_size << "\n";
    std::cout << "  Physical block size: " << info.physical_block_size << "\n";

    assert(info.logical_block_size > 0);
    assert(info.physical_block_size > 0);

    // Test alignment checks
    alignas(4096) char aligned_buf[8192];
    assert(DeviceDetector::is_aligned_for_direct_io(aligned_buf, 4096, 4096));
    assert(DeviceDetector::is_aligned_for_direct_io(aligned_buf, 8192, 4096));
    assert(!DeviceDetector::is_aligned_for_direct_io(aligned_buf + 1, 4096, 4096));
    assert(!DeviceDetector::is_aligned_for_direct_io(aligned_buf, 4095, 4096));

    std::cout << "  -> PASSED\n";
}

void test_aligned_buffer() {
    std::cout << "--- [Test 2] AlignedBuffer ---\n";
    AlignedBuffer buf(8192, 4096);
    assert(buf.capacity() >= 8192);
    assert(buf.capacity() % 4096 == 0);
    assert(reinterpret_cast<uintptr_t>(buf.data()) % 4096 == 0);

    // Test resizing and span
    buf.resize(100);
    assert(buf.size() == 100);
    assert(buf.span().size() == 100);

    std::memset(buf.data(), 0xAB, 100);
    assert(buf.byte_data()[0] == static_cast<std::byte>(0xAB));
    assert(buf.byte_data()[99] == static_cast<std::byte>(0xAB));

    // Test move
    AlignedBuffer moved = std::move(buf);
    assert(buf.data() == nullptr);
    assert(moved.size() == 100);
    assert(moved.byte_data()[0] == static_cast<std::byte>(0xAB));

    std::cout << "  -> PASSED\n";
}

core::Task<void> async_file_test(std::string test_file) {
    // Clean up old file
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    std::cout << "  [File Async] Creating file: " << test_file << "\n";
    File file = co_await File::create(test_file);
    assert(file.is_valid());

    std::string payload = "Hello, Aegon High-Performance Storage Engine!";
    auto data_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size());

    // Write
    size_t written = co_await file.write(0, data_span);
    assert(written == payload.size());

    // Sync
    co_await file.sync();

    // Check size
    uint64_t sz = co_await file.size();
    assert(sz == payload.size());

    // Read back
    std::vector<std::byte> read_buf(payload.size());
    co_await file.read_exact(0, read_buf);
    assert(std::memcmp(read_buf.data(), payload.data(), payload.size()) == 0);

    // Append test
    std::string extra = " [Appended Record]";
    auto extra_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(extra.data()), extra.size());
    size_t app_written = co_await file.append(extra_span);
    assert(app_written == extra.size());

    uint64_t new_sz = co_await file.size();
    assert(new_sz == payload.size() + extra.size());

    // Truncate test
    co_await file.truncate(10);
    uint64_t trunc_sz = co_await file.size();
    assert(trunc_sz == 10);

    // Close
    co_await file.close();
    assert(!file.is_valid());

    // Reopen in ReadOnly mode
    File read_file = co_await File::open(test_file, OpenMode::ReadOnly);
    assert(read_file.is_valid());
    std::vector<std::byte> truncated_buf(10);
    co_await read_file.read_exact(0, truncated_buf);
    assert(std::memcmp(truncated_buf.data(), payload.data(), 10) == 0);
    co_await read_file.close();

    std::filesystem::remove(test_file);
}

void test_file_basic_io() {
    std::cout << "--- [Test 3] File Async I/O (Default Engine) ---\n";
    core::EventLoop loop;
    auto tmp = (std::filesystem::temp_directory_path() / "test_storage_default.tmp").string();
    loop.spawn(async_file_test(tmp));
    loop.run();
    std::cout << "  -> PASSED\n";
}

void test_file_epoll_engine() {
    std::cout << "--- [Test 4] File Async I/O (Explicit Epoll Engine + ThreadPool) ---\n";
    core::EngineConfig config;
    config.type = core::EngineType::Epoll;

    core::EventLoop loop(config);
    assert(loop.engine().type() == core::EngineType::Epoll);

    auto tmp = (std::filesystem::temp_directory_path() / "test_storage_epoll.tmp").string();
    loop.spawn(async_file_test(tmp));
    loop.run();
    std::cout << "  -> PASSED\n";
}

core::Task<void> async_stream_test(std::string test_file) {
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    std::cout << "  [Streams Async] Testing chunked stream writes and reads...\n";
    File file = co_await File::create(test_file);

    // Write 256 KB of data in 16 KB chunks using FileWriteStream
    constexpr size_t TOTAL_BYTES = 256 * 1024;
    constexpr size_t WRITE_CHUNK = 16 * 1024;

    std::vector<std::byte> master_data(TOTAL_BYTES);
    for (size_t i = 0; i < TOTAL_BYTES; ++i) {
        master_data[i] = static_cast<std::byte>((i * 7 + 13) % 256);
    }

    {
        FileWriteStream write_stream(file, 32 * 1024);
        for (size_t offset = 0; offset < TOTAL_BYTES; offset += WRITE_CHUNK) {
            auto chunk_span = std::span<const std::byte>(master_data.data() + offset, WRITE_CHUNK);
            co_await write_stream.write_chunk(chunk_span);
        }
        co_await write_stream.flush();
    }

    uint64_t file_sz = co_await file.size();
    assert(file_sz == TOTAL_BYTES);

    // Read back in 24 KB chunks using FileReadStream
    constexpr size_t READ_CHUNK = 24 * 1024;
    FileReadStream read_stream(file, READ_CHUNK);

    std::vector<std::byte> read_back;
    read_back.reserve(TOTAL_BYTES);

    while (auto chunk = co_await read_stream.next_chunk()) {
        read_back.insert(read_back.end(), chunk->begin(), chunk->end());
    }

    assert(read_back.size() == TOTAL_BYTES);
    assert(std::memcmp(read_back.data(), master_data.data(), TOTAL_BYTES) == 0);
    assert(read_stream.is_eof());

    co_await file.close();
    std::filesystem::remove(test_file);
}

void test_file_streams() {
    std::cout << "--- [Test 5] File Streams (FileReadStream & FileWriteStream) ---\n";
    core::EventLoop loop;
    auto tmp = (std::filesystem::temp_directory_path() / "test_storage_stream.tmp").string();
    loop.spawn(async_stream_test(tmp));
    loop.run();
    std::cout << "  -> PASSED\n";
}

core::Task<void> async_wal_test(std::string test_file) {
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    std::cout << "  [WAL Async] Writing records to WAL...\n";
    WalLog wal = co_await WalLog::open(test_file);
    assert(wal.is_valid());

    uint64_t seq1 = co_await wal.append("ENTRY_1: User Created ID=42");
    uint64_t seq2 = co_await wal.append("ENTRY_2: Payment Processed $100.00");
    uint64_t seq3 = co_await wal.append("ENTRY_3: Order Shipped OrderID=987");

    assert(seq1 == 1);
    assert(seq2 == 2);
    assert(seq3 == 3);

    co_await wal.sync();
    co_await wal.close();

    // Reopen and recover
    std::cout << "  [WAL Async] Reopening and recovering WAL records...\n";
    WalLog recovered_wal = co_await WalLog::open(test_file);
    auto records = co_await recovered_wal.recover();

    assert(records.size() == 3);
    assert(recovered_wal.last_seq() == 3);

    std::string s1(reinterpret_cast<const char*>(records[0].data()), records[0].size());
    std::string s2(reinterpret_cast<const char*>(records[1].data()), records[1].size());
    std::string s3(reinterpret_cast<const char*>(records[2].data()), records[2].size());

    assert(s1 == "ENTRY_1: User Created ID=42");
    assert(s2 == "ENTRY_2: Payment Processed $100.00");
    assert(s3 == "ENTRY_3: Order Shipped OrderID=987");

    // Append one more record after recovery
    uint64_t seq4 = co_await recovered_wal.append("ENTRY_4: Post-recovery Record");
    assert(seq4 == 4);
    assert(recovered_wal.last_seq() == 4);

    co_await recovered_wal.close();
    std::filesystem::remove(test_file);
}

void test_wal_log() {
    std::cout << "--- [Test 6] WalLog (Write-Ahead Log & Crash Recovery) ---\n";
    core::EventLoop loop;
    auto tmp = (std::filesystem::temp_directory_path() / "test_storage_wal.tmp").string();
    loop.spawn(async_wal_test(tmp));
    loop.run();
    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   Aegon Persistent Storage Engine (aegon_storage) Tests\n";
    std::cout << "========================================================\n";

    test_device_detector();
    test_aligned_buffer();
    test_file_basic_io();
    test_file_epoll_engine();
    test_file_streams();
    test_wal_log();

    std::cout << "\n========================================================\n";
    std::cout << "   ALL AEGON STORAGE TESTS PASSED SUCCESSFULLY! (100%)\n";
    std::cout << "========================================================\n";
    return 0;
}
