/*
 * Jarvis RTOS — Kernel Log (dmesg) Ring Buffer Implementation
 */

#include <kernel/log/dmesg.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/task/scheduler.hpp>

namespace kernel::log {

DmesgBuffer<DMESG_CAPACITY> g_dmesg;

template<size_t Capacity>
bool DmesgBuffer<Capacity>::push(ErrorSubsystem subsys, uint64_t err_code,
                                  const char* msg, uintptr_t ctx) {
    const uint64_t ts = arch::Timer::ticks();
    const uint64_t tid = kernel::Scheduler::current_task()
                           ? kernel::Scheduler::current_task()->id
                           : 0;

    size_t h = atomic_load(&head, __ATOMIC_RELAXED);
    size_t t = atomic_load(&tail, __ATOMIC_ACQUIRE);

    size_t next_h = (h + 1) & MASK;
    bool overwrote = false;

    if (next_h == t) {
        overwrote = true;
        atomic_store(&tail, (t + 1) & MASK, __ATOMIC_RELEASE);
    }

    buffer[h] = LogEntry{ts, tid, subsys, err_code, ctx, msg};

    atomic_store(&head, next_h, __ATOMIC_RELEASE);
    return !overwrote;
}

template<size_t Capacity>
bool DmesgBuffer<Capacity>::pop(LogEntry& entry) {
    size_t t = atomic_load(&tail, __ATOMIC_RELAXED);
    size_t h = atomic_load(&head, __ATOMIC_ACQUIRE);

    if (t == h) return false;

    entry = buffer[t];
    atomic_store(&tail, (t + 1) & MASK, __ATOMIC_RELEASE);
    return true;
}

template<size_t Capacity>
bool DmesgBuffer<Capacity>::empty() const {
    return atomic_load(&head, __ATOMIC_ACQUIRE) ==
           atomic_load(&tail, __ATOMIC_ACQUIRE);
}

template<size_t Capacity>
size_t DmesgBuffer<Capacity>::size() const {
    size_t h = atomic_load(&head, __ATOMIC_ACQUIRE);
    size_t t = atomic_load(&tail, __ATOMIC_ACQUIRE);
    return (h - t) & MASK;
}

template<size_t Capacity>
void DmesgBuffer<Capacity>::clear() {
    size_t h = atomic_load(&head, __ATOMIC_RELAXED);
    atomic_store(&tail, h, __ATOMIC_RELEASE);
}

template class DmesgBuffer<DMESG_CAPACITY>;

} // namespace kernel::log