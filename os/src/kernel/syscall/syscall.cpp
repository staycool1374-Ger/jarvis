#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/arch/msr.hpp>
#include <constants.hpp>

extern "C" void syscall_entry();

namespace kernel {

void Syscall::init() {
    uint64_t star_val = (static_cast<uint64_t>(arch::SEG_KERNEL_CODE) << 32) |
                        (static_cast<uint64_t>(arch::SEG_USER_CODE) << 48);
    arch::wrmsr(arch::IA32_STAR, star_val);
    arch::wrmsr(arch::IA32_LSTAR, reinterpret_cast<uint64_t>(syscall_entry));
    arch::wrmsr(arch::IA32_FMASK, 0x200);
}

TaskControlBlock* syscall_task() {
    return Scheduler::current_task();
}

bool syscall_is_user_task() {
    auto* t = syscall_task();
    return t && t->page_table_ != 0;
}

int syscall_task_open(vfs::Vnode* vn, uint64_t flags) {
    int fd = syscall_task()->fd_table.alloc();
    if (fd < 0) return -1;
    syscall_task()->fd_table.fds[fd].vnode = vn;
    syscall_task()->fd_table.fds[fd].offset = 0;
    syscall_task()->fd_table.fds[fd].flags = flags;
    if (vn->ops->open) vn->ops->open(vn, flags);
    return fd;
}

int syscall_path_open(const char* path, uint64_t flags) {
    vfs::Vnode* vn = vfs::resolve(path);
    if (!vn) return -1;
    return syscall_task_open(vn, flags);
}

uint64_t Syscall::handle(uint64_t number, uint64_t arg0, uint64_t arg1,
                         uint64_t arg2, uint64_t arg3, uint64_t* regs)
{
    if (number >= static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL))
        return static_cast<uint64_t>(-1);
    return syscall_table_[number](arg0, arg1, arg2, arg3, regs);
}

} // namespace kernel
