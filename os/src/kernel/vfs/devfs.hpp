#pragma once

#include <kernel/vfs/vfs.hpp>

namespace kernel {
namespace vfs {

extern Filesystem dev_fs;

/// @brief Initialize the device filesystem, registering device nodes.
void devfs_init();

} // namespace vfs
} // namespace kernel
