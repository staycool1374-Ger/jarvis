#pragma once

#include <kernel/vfs/vfs.hpp>

namespace kernel {
namespace vfs {

extern Filesystem tmpfs_fs;
void tmpfs_reset_root();

} // namespace vfs
} // namespace kernel
