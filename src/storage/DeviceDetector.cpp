#include "storage/DeviceDetector.h"
#include "log/Logger.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <mutex>

#if defined(__linux__)
#include <sys/sysmacros.h>
#endif

namespace aegon::storage {

namespace {

static std::unordered_map<uint64_t, DeviceInfo> s_device_cache;
static std::mutex s_cache_mutex;

static std::string read_sysfs_string(const std::filesystem::path& p) {
    std::ifstream f(p);
    if (!f.is_open()) return {};
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    return line;
}

static uint64_t read_sysfs_uint(const std::filesystem::path& p, uint64_t default_val = 0) {
    std::string s = read_sysfs_string(p);
    if (s.empty()) return default_val;
    try {
        return std::stoull(s);
    } catch (...) {
        return default_val;
    }
}

static DeviceInfo probe_dev_t(dev_t dev) {
    uint64_t key = static_cast<uint64_t>(dev);
    {
        std::lock_guard<std::mutex> lock(s_cache_mutex);
        auto it = s_device_cache.find(key);
        if (it != s_device_cache.end()) {
            return it->second;
        }
    }

    DeviceInfo info{};
    info.logical_block_size = 512;
    info.physical_block_size = 4096;
    info.optimal_io_size = 64 * 1024;
    info.supports_direct_io = true;

#if defined(__linux__)
    unsigned int maj = major(dev);
    unsigned int min = minor(dev);

    std::filesystem::path sys_dev_path = std::filesystem::path("/sys/dev/block") / 
                                        (std::to_string(maj) + ":" + std::to_string(min));

    if (std::filesystem::exists(sys_dev_path)) {
        std::error_code ec;
        auto canonical_path = std::filesystem::canonical(sys_dev_path, ec);
        if (!ec) {
            std::filesystem::path queue_dir = canonical_path / "queue";
            // If partition (e.g. nvme0n1p1 or sda1), the queue directory lives on the parent block device
            if (!std::filesystem::exists(queue_dir)) {
                queue_dir = canonical_path.parent_path() / "queue";
            }

            if (std::filesystem::exists(queue_dir)) {
                uint64_t rot = read_sysfs_uint(queue_dir / "rotational", 0);
                info.rotational = (rot != 0);

                uint64_t lbs = read_sysfs_uint(queue_dir / "logical_block_size", 512);
                if (lbs > 0) info.logical_block_size = lbs;

                uint64_t pbs = read_sysfs_uint(queue_dir / "physical_block_size", 4096);
                if (pbs > 0) info.physical_block_size = pbs;

                uint64_t opt = read_sysfs_uint(queue_dir / "optimal_io_size", 0);
                if (opt > 0) info.optimal_io_size = opt;
            }

            std::string name = canonical_path.filename().string();
            info.device_name = name;

            if (name.find("nvme") != std::string::npos || canonical_path.string().find("nvme") != std::string::npos) {
                info.tier = DeviceTier::NVMe;
                info.rotational = false;
                info.supports_direct_io = true;
            } else if (!info.rotational) {
                info.tier = DeviceTier::SATA_SSD;
                info.supports_direct_io = true;
            } else {
                info.tier = DeviceTier::HDD;
                info.supports_direct_io = true;
            }
        }
    } else {
        info.tier = DeviceTier::Unknown;
    }
#else
    info.tier = DeviceTier::Unknown;
#endif

    {
        std::lock_guard<std::mutex> lock(s_cache_mutex);
        s_device_cache[key] = info;
    }

    return info;
}

} // namespace

DeviceInfo DeviceDetector::probe_fd(int fd) {
    if (fd < 0) return {};
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        return {};
    }
    return probe_dev_t(st.st_dev);
}

DeviceInfo DeviceDetector::probe_path(std::string_view path) {
    struct stat st{};
    if (::stat(std::string(path).c_str(), &st) != 0) {
        // Path might not exist yet; try parent directory
        auto parent = std::filesystem::path(path).parent_path();
        if (parent.empty()) parent = ".";
        if (::stat(parent.string().c_str(), &st) != 0) {
            return {};
        }
    }
    return probe_dev_t(st.st_dev);
}

bool DeviceDetector::is_aligned_for_direct_io(const void* ptr, size_t size, size_t alignment) noexcept {
    if (!ptr || alignment == 0) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    return (addr % alignment == 0) && (size % alignment == 0);
}

} // namespace aegon::storage

