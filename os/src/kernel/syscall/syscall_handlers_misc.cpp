#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/rtc.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <string.hpp>
#include <version.hpp>

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

uint64_t Syscall::sys_get_ticks(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*) {
    return arch::Timer::ticks();
}

uint64_t Syscall::sys_getpid(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*) {
    auto* t = syscall_task();
    return t ? t->id : 0;
}

uint64_t Syscall::sys_exit(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*) {
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
                            auto* stack = reinterpret_cast<uint64_t*>(p->context.rsp);
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

uint64_t Syscall::sys_gettod(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*) {
    auto tv_ptr = checked(reinterpret_cast<Timeval*>(arg0));
    if (syscall_is_user_task() && !tv_ptr.valid()) return static_cast<uint64_t>(-1);

    uint64_t secs = arch::RTC::read_seconds();
    Timeval tv = {};
    tv.tv_sec = static_cast<int64_t>(secs);
    tv.tv_usec = 0;
    *tv_ptr.unsafe_ptr() = tv;
    return 0;
}

uint64_t Syscall::sys_uname(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*) {
    auto uts_ptr = checked(reinterpret_cast<Utsname*>(arg0));
    if (syscall_is_user_task() && !uts_ptr.valid()) return static_cast<uint64_t>(-1);

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

} // namespace kernel
