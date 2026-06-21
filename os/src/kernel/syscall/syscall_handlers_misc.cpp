#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/log/ring_buffer.hpp>
#include <kernel/random.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/rtc.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/sync/spinlock_guard.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <string.hpp>
#include <version.hpp>
#include <constants.hpp>

namespace kernel {

struct Timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct Utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

uint64_t Syscall::sys_yield(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*) {
    arch::io_wait();
    Scheduler::reschedule();
    return 0;
}

uint64_t Syscall::sys_print(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*) {
    return 0;
}

uint64_t Syscall::sys_get_ticks(uint64_t, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    return arch::Timer::ticks();
}

uint64_t Syscall::sys_getpid(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*
    ) {
    auto* t = syscall_task();
    return t ? t->id : 0;
}

uint64_t Syscall::sys_exit(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    auto* t = syscall_task();
    if (t) {
        t->state = TaskState::TERMINATED;
        t->exit_code = arg0;

        if (t->first_child) {
            TaskControlBlock* init_task = Scheduler::task_at(0);
            if (init_task) {
                auto* child = t->first_child;
                while (child) {
                    auto* next = child->next_sibling;
                    t->remove_child(child);
                    init_task->add_child(child);
                    child = next;
                }
            }
        }

        if (t->parent_id) {
            uint64_t count = Scheduler::task_count();
            for (uint64_t i = 0; i < count; ++i) {
                auto* p = Scheduler::task_at(i);
                if (p && p->id == t->parent_id &&
                    (p->waiting_child_pid == t->id ||
                     p->waiting_child_pid == static_cast<uint64_t>(-1))) {
                    // Write exit code to parent's user-space status pointer.
                    // We must switch to the parent's page table because
                    // the status address belongs to the parent's address space.
                    if (p->waiting_child_status) {
                        uint64_t old_cr3 = 0;
                        bool switched = false;
                        if (p->page_table_ &&
                            arch::read_cr3() != p->page_table_) {
                            old_cr3 = arch::read_cr3();
                            arch::write_cr3(p->page_table_);
                            switched = true;
                        }
                        *p->waiting_child_status = t->exit_code;
                        if (switched) arch::write_cr3(old_cr3);
                        p->waiting_child_status = nullptr;
                    }
                    p->waiting_child_pid = 0;
                    // Orphan the child so reap_orphans can clean it up
                    p->remove_child(t);
                    t->parent_id = 0;
                    // Wake the parent and override its saved RAX to return
                    // the child's PID instead of -1 (the value set when
                    // waitpid blocked).
                    if (p->state != TaskState::TERMINATED) {
                        p->state = TaskState::READY;
                        if (p->context.rsp) {
                            auto* stack = reinterpret_cast<uint64_t*>(
                                p->context.rsp);
                            stack[0] = t->id;
                        }
                    }
                    break;
                }
            }
        }
    }
    return 0;
}

uint64_t Syscall::sys_gettod(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    auto tv_ptr = checked(reinterpret_cast<Timeval*>(arg0));
    if (syscall_is_user_task() && !tv_ptr.valid()) return static_cast<uint64_t>(
        -1);

    uint64_t secs = arch::RTC::read_seconds();
    Timeval tv = {};
    tv.tv_sec = static_cast<int64_t>(secs);
    tv.tv_usec = 0;
    *tv_ptr.unsafe_ptr() = tv;
    return 0;
}

uint64_t Syscall::sys_uname(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    auto uts_ptr = checked(reinterpret_cast<Utsname*>(arg0));
    if (syscall_is_user_task() && !uts_ptr.valid()
        ) return static_cast<uint64_t>(-1);

    Utsname uts{};
    strlcpy(uts.sysname, "Jarvis", sizeof(uts.sysname));
    strlcpy(uts.nodename, "jarvis", sizeof(uts.nodename));
    strlcpy(uts.release, Version::string(), sizeof(uts.release));
    strlcpy(uts.version, Version::build_date(), sizeof(uts.version));
    strlcpy(uts.machine, "x86_64", sizeof(uts.machine));
    strlcpy(uts.domainname, "(none)", sizeof(uts.domainname));
    *uts_ptr.unsafe_ptr() = uts;
    return 0;
}

uint64_t Syscall::sys_pause(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*) {
    arch::hlt();
    return 0;
}

uint64_t Syscall::sys_brk(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*) {
    static sync::SpinLock brk_lock;
    SpinLockGuard<sync::SpinLock> guard(brk_lock);
    auto* t = syscall_task();
    if (!t) return static_cast<uint64_t>(-1);

    // Query mode
    if (arg0 == 0) return t->program_break;

    // Cannot shrink below start
    if (arg0 < t->program_break_start)
        return t->program_break;

    // Cannot expand beyond stack area
    if (arg0 > mem::STACK_VADDR)
        return static_cast<uint64_t>(-1);

    uint64_t old_break = t->program_break;

    // Expand: map new pages
    if (arg0 > old_break) {
        uint64_t start_page = (old_break + arch::PAGE_SIZE - 1) & ~(arch::PAGE_SIZE - 1);
        uint64_t end_page = (arg0 + arch::PAGE_SIZE - 1) & ~(arch::PAGE_SIZE - 1);
        for (uint64_t vaddr = start_page; vaddr < end_page; vaddr += arch::PAGE_SIZE) {
            // Check if already mapped
            if (VMM::virt_to_phys_in_pml4(vaddr, t->page_table_) != 0)
                continue;
            uint64_t phys = PMM::alloc_user_page();
            if (!phys) return static_cast<uint64_t>(-1);
            VMM::map_page_in_pml4(vaddr, phys, true, t->page_table_);
            __builtin_memset(reinterpret_cast<void*>(arch::HHDM_OFFSET + phys), 0, arch::PAGE_SIZE);
        }
    }

    // Contract: could unmap pages but it's optional — leave them mapped
    t->program_break = arg0;
    return t->program_break;
}

struct Rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

enum RlimitResource {
    RLIMIT_DATA = 0,
    RLIMIT_STACK = 1,
    RLIMIT_NOFILE = 2,
};

uint64_t Syscall::sys_getrlimit(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t, uint64_t*) {
    auto* t = syscall_task();
    if (!t) return static_cast<uint64_t>(-1);

    Rlimit rl = {};
    switch (arg0) {
    case RLIMIT_DATA:
        rl.rlim_cur = mem::STACK_VADDR - t->program_break_start;
        rl.rlim_max = mem::STACK_VADDR - t->program_break_start;
        break;
    case RLIMIT_STACK:
        rl.rlim_cur = mem::STACK_SIZE;
        rl.rlim_max = mem::STACK_SIZE;
        break;
    case RLIMIT_NOFILE:
        rl.rlim_cur = vfs::MAX_FDS;
        rl.rlim_max = vfs::MAX_FDS;
        break;
    default:
        return static_cast<uint64_t>(-1);
    }

    auto rl_ptr = checked(reinterpret_cast<Rlimit*>(arg1));
    if (syscall_is_user_task() && !rl_ptr.valid()) return static_cast<uint64_t>(-1);
    *rl_ptr.unsafe_ptr() = rl;
    return 0;
}

uint64_t Syscall::sys_setrlimit(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t, uint64_t*) {
    auto* t = syscall_task();
    if (!t) return static_cast<uint64_t>(-1);

    auto rl_ptr = checked(reinterpret_cast<const Rlimit*>(arg1));
    if (syscall_is_user_task() && !rl_ptr.valid()) return static_cast<uint64_t>(-1);
    Rlimit rl = *rl_ptr.unsafe_ptr();
    (void)rl;
    (void)arg0;
    return 0;
}

uint64_t Syscall::sys_getrandom(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t*) {
    // arg0 = buffer, arg1 = length, arg2 = flags (reserved, must be 0)
    if (arg2 != 0) return static_cast<uint64_t>(-1);
    if (arg1 == 0) return 0;

    auto buf = checked(reinterpret_cast<uint8_t*>(arg0), arg1);
    if (!buf.valid()) return static_cast<uint64_t>(-1);

    random_fill(buf.unsafe_ptr(), static_cast<size_t>(arg1));
    return arg1;
}

uint64_t Syscall::sys_klog(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t*) {
    // arg0 = buffer, arg1 = size, arg2 = flags (0=read, 1=clear)
    if (arg2 == 1) {
        kernel::log::g_klog.clear();
        return 0;
    }
    if (arg0 == 0 || arg1 == 0) return 0;

    if (syscall_is_user_task()) {
        auto buf = checked(reinterpret_cast<char*>(arg0), arg1);
        if (!buf.valid()) return static_cast<uint64_t>(-1);
        return kernel::log::g_klog.read(buf.unsafe_ptr(), static_cast<size_t>(arg1));
    }

    return kernel::log::g_klog.read(reinterpret_cast<char*>(arg0), static_cast<size_t>(arg1));
}

} // namespace kernel
