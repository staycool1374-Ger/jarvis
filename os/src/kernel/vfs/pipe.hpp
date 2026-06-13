#pragma once

#include <types.hpp>

namespace kernel {
namespace vfs {

/// @brief Create a pair of connected pipe file descriptors.
/// @param[out] fds Array of two descriptors: fds[0] for read, fds[1] for write.
/// @return 0 on success, or VFS_INVALID on failure.
int create_pipe(int fds[2]);

} // namespace vfs
} // namespace kernel