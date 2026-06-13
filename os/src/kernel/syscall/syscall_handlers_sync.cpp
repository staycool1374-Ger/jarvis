#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/memory/checked_ptr.hpp>

namespace kernel {

uint64_t Syscall::sys_notify(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    uint64_t target_id = arg0;
    uint64_t value = arg1;
    auto* t = Scheduler::find_task(target_id);
    if (!t || !t->notify) return static_cast<uint64_t>(-1);
    t->notify->notify(value);
    return 0;
}

uint64_t Syscall::sys_notify_wait(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    auto* cur = syscall_task();
    if (!cur || !cur->notify) return static_cast<uint64_t>(-1);
    uint64_t value = cur->notify->wait();
    auto val_ptr = checked(reinterpret_cast<uint64_t*>(arg0));
    if (syscall_is_user_task() && !val_ptr.valid()
        ) return static_cast<uint64_t>(-1);
    val_ptr.write(value);
    return 0;
}

uint64_t Syscall::sys_event_set(uint64_t arg0, uint64_t arg1, uint64_t,
    uint64_t, uint64_t*) {
    uint64_t target_id = arg0;
    uint64_t bits = arg1;
    auto* t = Scheduler::find_task(target_id);
    if (!t || !t->event_group) return static_cast<uint64_t>(-1);
    t->event_group->set_bits(bits);
    return 0;
}

uint64_t Syscall::sys_event_wait(uint64_t arg0, uint64_t arg1, uint64_t,
    uint64_t, uint64_t*) {
    uint64_t wanted = arg0;
    uint64_t clear_on_exit = arg1;
    auto* cur = syscall_task();
    if (!cur || !cur->event_group) return static_cast<uint64_t>(-1);
    cur->event_group->wait_bits(wanted, clear_on_exit != 0);
    return 0;
}

uint64_t Syscall::sys_alarm(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);

    uint64_t seconds = arg0;
    uint64_t microseconds = arg1;

    if (seconds == 0 && microseconds == 0) {
        cur->alarm_armed = false;
        return 0;
    }

    uint64_t current_tick = arch::Timer::ticks();
    uint64_t ticks = seconds * 1000 + (microseconds + 999) / 1000;
    cur->alarm_ticks = current_tick + ticks;
    cur->alarm_armed = true;
    return 0;
}

} // namespace kernel
