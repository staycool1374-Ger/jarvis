#pragma once

#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/fat32.hpp>

namespace kernel {
namespace vfs {

/// @brief The FAT32 filesystem instance.  Set `partition` before boot.
extern fat32::Fat32Partition* fat32_partition_instance;

/// @brief Mount the FAT32 filesystem at the given mount point.
/// @return 0 on success, VFS_INVALID on error.
int mount_fat32(const char* mount_point);

/// @brief VFS filesystem descriptor for FAT32.
extern Filesystem fat32_fs;

} // namespace vfs
} // namespace kernel
