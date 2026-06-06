#pragma once

#include <kernel/vfs/vfs.hpp>

namespace kernel {
namespace vfs {

extern Filesystem dev_fs;

void devfs_init();

void tty_wake_readers();

} // namespace vfs
} // namespace kernel
