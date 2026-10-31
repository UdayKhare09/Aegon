#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace aegon::storage {

/**
 * @brief Identifies the I/O engine execution path for persistent storage operations.
 */
enum class EngineBackend : uint8_t {
    Auto,       ///< Uses io_uring direct submission if supported, else worker threadpool
    IoUring,    ///< Explicitly force io_uring ring submission
    ThreadPool  ///< Force background threadpool (epoll engine model)
};

/**
 * @brief Policy for O_DIRECT / unbuffered direct hardware I/O.
 */
enum class DirectIoMode : uint8_t {
    Auto,       ///< Enable O_DIRECT if device is NVMe/SSD and buffer memory is block-aligned
    Always,     ///< Require O_DIRECT (fails open/write if alignment is invalid)
    Disabled    ///< Always use buffered pagecache I/O
};

/**
 * @brief Detected underlying storage device tier.
 */
enum class DeviceTier : uint8_t {
    NVMe,       ///< Non-Volatile Memory Express (PCIe bus, ultra-low latency, multi-queue)
    SATA_SSD,   ///< Solid State Drive over SATA/SAS (non-rotational)
    HDD,        ///< Rotational magnetic spinning hard drive
    Remote,     ///< NFS, SMB, Ceph, or network-attached mount
    Unknown     ///< Virtual filesystem, RAM disk (tmpfs), or unprobed
};

/**
 * @brief Bitflag options for opening files.
 */
enum class OpenMode : uint32_t {
    None       = 0,
    ReadOnly   = 1 << 0,  ///< Open for reading
    WriteOnly  = 1 << 1,  ///< Open for writing
    ReadWrite  = ReadOnly | WriteOnly, ///< Open for reading and writing
    Create     = 1 << 2,  ///< Create file if it does not exist
    Exclusive  = 1 << 3,  ///< Fail if file exists with Create (O_EXCL)
    Truncate   = 1 << 4,  ///< Truncate existing file to 0 length
    Append     = 1 << 5,  ///< Seek to end before each write
    Direct     = 1 << 6,  ///< Request O_DIRECT (bypassing OS pagecache)
    Sync       = 1 << 7   ///< Synchronous data & metadata writes (O_SYNC / O_DSYNC)
};

[[nodiscard]] constexpr OpenMode operator|(OpenMode a, OpenMode b) noexcept {
    return static_cast<OpenMode>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr OpenMode operator&(OpenMode a, OpenMode b) noexcept {
    return static_cast<OpenMode>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr OpenMode operator~(OpenMode a) noexcept {
    return static_cast<OpenMode>(~static_cast<uint32_t>(a));
}

[[nodiscard]] constexpr bool has_flag(OpenMode mask, OpenMode flag) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(flag)) == static_cast<uint32_t>(flag);
}

/**
 * @brief Complete persistent storage configuration for aegon_storage.
 */
struct StorageConfig {
    EngineBackend backend{EngineBackend::Auto};
    DirectIoMode direct_io{DirectIoMode::Auto};

    // Buffer & alignment defaults
    size_t default_chunk_size{64 * 1024};    ///< Default streaming chunk size (64 KB)
    size_t direct_io_alignment{4096};        ///< Sector / memory alignment for Direct I/O (4 KB)

    // Advanced io_uring settings
    bool register_files{false};              ///< Register open file descriptors in io_uring fixed table
    bool register_buffers{false};            ///< Register I/O buffer pools with io_uring
    uint32_t sq_poll_cpu{0};                 ///< Kernel submission queue polling CPU core (0 = disabled)

    // Epoll fallback thread pool
    size_t fallback_threads{4};              ///< Number of worker threads for blocking storage operations
};

} // namespace aegon::storage

