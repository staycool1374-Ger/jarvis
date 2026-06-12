#include <kernel/driver/iocd.hpp>
#include <kernel/task/scheduler.hpp>

namespace kernel {
namespace iocd {

static uint64_t g_iocd_pid = 0;

void set_iocd_pid(uint64_t pid) {
    g_iocd_pid = pid;
}

uint64_t get_iocd_pid() {
    return g_iocd_pid;
}

bool is_iocd_task() {
    auto* cur = Scheduler::current_task();
    return cur && cur->id == g_iocd_pid;
}

} // namespace iocd
} // namespace kernel
