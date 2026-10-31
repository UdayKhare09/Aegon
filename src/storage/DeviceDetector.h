#pragma once

#include "storage/StorageConfig.h"
#include <string>
#include <string_view>
#include <cstdint>
#include <cstddef>

namespace aegon::storage {

/**
 * @brief Hardware characteristics of the block storage device hosting a file.
 */
struct DeviceInfo {
    DeviceTier tier{DeviceTier::Unknown};
    bool rotational{false};
    size_t logical_block_size{512};
    size_t physical_block_size{4096};
    size_t optimal_io_size{64 * 1024};
    bool supports_direct_io{false};
    std::string device_name;
    std::string mount_point;
};

/**
 * @brief Automatic storage device discovery and sysfs hardware inspector.
 */
class DeviceDetector {
public:
    /**
     * @brief Probes device properties by file descriptor.
     */
    static DeviceInfo probe_fd(int fd);

    /**
     * @brief Probes device properties by filesystem path.
     */
    static DeviceInfo probe_path(std::string_view path);

    /**
     * @brief Checks if a buffer pointer and size satisfy the alignment requirements for O_DIRECT.
     */
    [[nodiscard]] static bool is_aligned_for_direct_io(const void* ptr, size_t size, size_t alignment = 4096) noexcept;
};

} // namespace aegon::storage

