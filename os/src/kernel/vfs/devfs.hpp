#pragma once

#include <kernel/vfs/vfs.hpp>

namespace kernel {
namespace vfs {

extern Filesystem dev_fs;

void devfs_init();

} // namespace vfs
} // namespace kernel
