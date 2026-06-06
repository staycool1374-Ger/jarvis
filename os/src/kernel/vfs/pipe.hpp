#pragma once

#include <types.hpp>

namespace kernel {
namespace vfs {

int create_pipe(int fds[2]);

} // namespace vfs
} // namespace kernel