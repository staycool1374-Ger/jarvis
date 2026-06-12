#include <kernel/vfs/vfsd.hpp>
#include <kernel/task/scheduler.hpp>

namespace kernel {
namespace vfsd {

static uint64_t g_vfsd_pid = 0;

void set_vfsd_pid(uint64_t pid) {
    g_vfsd_pid = pid;
}

uint64_t get_vfsd_pid() {
    return g_vfsd_pid;
}

bool is_vfsd_task() {
    auto* cur = Scheduler::current_task();
    return cur && cur->id == g_vfsd_pid;
}

} // namespace vfsd
} // namespace kernel