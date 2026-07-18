/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// @file scheduler.cpp
/// @brief Scheduler implementation: task lifecycle, ready-queue management,
///        rate-monotonic dispatch, context switching, and test isolation.

#include <kernel/task/scheduler.hpp>
#include <kernel/arch/gdt.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/hal/irq_guard.hpp>
#include <kernel/sync/spinlock_guard.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/debug/ipc_sched_trace.hpp>

extern "C" void debug_write(const char *s);
extern "C" void debug_write_hex(uint64_t value);
extern "C" void debug_write_dec(uint64_t value);
#include <kernel/memory/integrity.hpp>
#include <kernel/test/resource_tracker.hpp>
#include <kernel/test/test_watchdog.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/driver/iocd.hpp>

// Architecture-aware stack pointer access
#if defined(CONFIG_ARCH_X86_64)
#define TASK_STACK_PTR(t) ((t)->context.rsp)
#elif defined(CONFIG_ARCH_AARCH64)
#define TASK_STACK_PTR(t) ((t)->context.sp_el0)
#elif defined(CONFIG_ARCH_RISCV64)
#define TASK_STACK_PTR(t) ((t)->context.sp)
#endif
#include <kernel/task/sporadic_server.hpp>
#include <signal.hpp>
#include <logger.hpp>
#include <assert.hpp>

#if defined(CONFIG_DEBUG_IPC_SCHED)
// Wedge diagnostics for deferred-switch / ready-queue desync analysis.
static uint64_t s_wedge_emitted_ = 0;      // throttle [WEDGE] emissions
#endif

extern "C" {
uint64_t scheduler_dummy_save_rsp = 0;
bool s_reap_in_progress = false;
}

namespace kernel {

// P5a: Deferred-kill list.
static constexpr uint64_t MAX_DEFERRED_KILLS = 16;
static TaskControlBlock *s_deferred_kill_tasks[MAX_DEFERRED_KILLS] = {};
static uint64_t s_deferred_kill_count = 0;

static inline uint64_t effective_priority(const TaskControlBlock *t) noexcept {
    if (t && t->sporadic_server)
        return t->sporadic_server->current_priority();
    return t ? t->priority : 0;
}

void Scheduler::enqueue_ready(TaskControlBlock &task) noexcept {
    ready_queue_.enqueue(task, effective_priority(&task));
}

void Scheduler::dequeue_ready(TaskControlBlock &task) noexcept {
    ready_queue_.remove(task, effective_priority(&task));
}

void Scheduler::set_task_ready(TaskControlBlock &task) noexcept {
    task.state = TaskState::READY;
    enqueue_ready(task);
}

/// @brief Wake a parent blocked in waitpid for a child that just terminated.
/// Mirrors the wake performed by Syscall::sys_exit, but for non-sys_exit
/// termination paths (Scheduler::terminate, deadline miss, cleanup_test_tasks)
/// so a parent blocked in waitpid is not left waiting forever on a child that
/// exited via a path other than sys_exit.
/// @param child The just-terminated child task.
static void wake_waiting_parent(TaskControlBlock &child) {
    if (child.parent_id == 0)
        return;
    auto *p = Scheduler::find_task(child.parent_id);
    if (!p)
        return;
    if (p->waiting_child_pid != child.id)
        return;

    // Deliver the child's exit status to a parent blocked in waitpid.
    if (p->waiting_child_status) {
        uint64_t old_cr3 = 0;
        bool switched = false;
        if (p->page_table_ && arch::read_cr3() != p->page_table_) {
            old_cr3 = arch::read_cr3();
            arch::write_cr3(p->page_table_);
            switched = true;
        }
        *p->waiting_child_status = child.exit_code;
        if (switched)
            arch::write_cr3(old_cr3);
        p->waiting_child_status = nullptr;
    }

    // Clear the parent's wait and orphan the child for ANY waiting parent.
    // This lets reap_orphans collect the zombie even when the parent is a
    // manual-wait task (READY) that will never re-scan and reap it itself —
    // otherwise the child stays deferred forever and the scheduler deadlocks.
    // Only a BLOCKED parent is re-enqueued: a READY/WAITING parent is already
    // on the run queue, and re-enqueueing it would corrupt the intrusive
    // ready-queue links.
    if (p->state == TaskState::BLOCKED) {
        Scheduler::set_task_ready(*p);
        if (TASK_STACK_PTR(p)) {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            auto *stack = reinterpret_cast<uint64_t *>(TASK_STACK_PTR(p));
            stack[0] = child.id;
        }
    }
    p->waiting_child_pid = 0;
    p->remove_child(&child);
    child.parent_id = 0;
}

// Forward declaration — defined later in this translation unit.
static void switch_to_task(TaskControlBlock *current, TaskControlBlock *next,
                           sync::SpinLock *held_lock);

void Scheduler::terminate(TaskControlBlock &task, uint64_t exit_code) noexcept {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    dequeue_ready(task);
    task.state = TaskState::TERMINATED;
    task.exit_code = exit_code;
    // If the parent is blocked in waitpid for this child, wake it now so it
    // does not deadlock waiting for a child that exited without sys_exit.
    wake_waiting_parent(task);

    // If the terminating task is the one currently on the CPU, arrange for a
    // context switch to a valid successor on the next ISR.  Otherwise
    // current_task_ptr_ stays parked on a TERMINATED task and the running RSP
    // is later saved into its dead context, which can wedge the scheduler in
    // the idle loop (observed as the `all` suite hanging at the atomic
    // context-switch tests).
    if (&task == current_task_ptr_) {
        auto *next = next_task();
        if (next && next != &task) {
            switch_to_task(&task, next, nullptr);
        }
    }
}

TaskControlBlock *const Scheduler::ID_TOMBSTONE =
    reinterpret_cast<TaskControlBlock *>(static_cast<uintptr_t>(1));

AllTasksRegistry Scheduler::all_tasks_;
constinit TaskControlBlock *Scheduler::current_task_ptr_ = nullptr;
constinit TaskControlBlock *Scheduler::id_table_[ID_TABLE_SIZE] = {};

constinit uint64_t Scheduler::next_task_id_ = 0;
constinit uint64_t Scheduler::sporadic_task_count_ = 0;
constinit bool Scheduler::preempt_enabled_ = false;
constinit bool Scheduler::suppress_terminated_log_ = false;
#if CONFIG_DEADLINE_MONITOR_TASK
constinit TaskControlBlock *Scheduler::s_monitor_task_ = nullptr;
volatile bool Scheduler::s_scan_requested_ = false;
bool Scheduler::s_test_active_ = false;
uint64_t g_test_deadline_monitor_pid = 0;
#endif
ReadyQueueManager Scheduler::ready_queue_;
DeadlineList Scheduler::deadline_list_;
constinit TaskControlBlock *Scheduler::idle_task_ = nullptr;
constinit TaskControlBlock *Scheduler::shell_task_ptr_ = nullptr;
sync::SpinLock Scheduler::scheduler_lock_;

// Liu-Leyland Rate-Monotonic LUB bounds (scaled by 1000000)
static constexpr uint64_t LIU_LEYLAND_MAX_TASKS = 20;
static constexpr uint32_t LIU_LEYLAND_BOUNDS[LIU_LEYLAND_MAX_TASKS + 1] = {
    0,      1000000, 828427, 779763, 756828, 743491, 734772,
    728626, 724061,  720537, 717734, 715451, 713557, 711958,
    710592, 709409,  708378, 707472, 706669, 705952, 705298};
static constexpr uint32_t LIU_LEYLAND_LIMIT = 693147;

// ---------------------------------------------------------------------------
// Init / lifecycle
// ---------------------------------------------------------------------------

void Scheduler::init() {
    for (uint64_t i = 0; i < ID_TABLE_SIZE; ++i)
        id_table_[i] = nullptr;

    idle_task_ = TaskControlBlock::create(kernel::integrity::idle_task_main, 0,
                                          0xFFFFFFFF);
    idle_task_->state = TaskState::READY;

    all_tasks_.append(idle_task_);
    id_table_insert(idle_task_->id, idle_task_);
    current_task_ptr_ = idle_task_;
    sporadic_task_count_ = 0;
    preempt_enabled_ = true;

#if CONFIG_DEADLINE_MONITOR_TASK
    ensure_monitor();
#endif
}

void Scheduler::add_task(TaskControlBlock &task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    ENSURE(task.state == TaskState::READY);
    all_tasks_.append(&task);
    if (task.period_ticks > 0 && task.deadline_ticks > 0) {
        deadline_list_.insert(&task);
    }
    id_table_insert(task.id, &task);
    task.in_ready_queue_ = false;
    task.runq_next_ = nullptr;
    task.runq_prev_ = nullptr;
    ready_queue_.enqueue(task, effective_priority(&task));
    kernel::test::ResourceTracker::instance().track_task_add();

    Logger::info("Scheduler: task '%s' (ID=%u, prio=%u) started", task.name,
                 task.id, task.priority);

    // Liu-Leyland LUB admission test
    if (task.period_ticks > 0 && task.period_ticks <= 100) {
        uint64_t total_util = 0;
        uint64_t real_tasks = 0;
        TaskControlBlock *t = all_tasks_.first_ptr();
        for (; t; t = all_tasks_.next_ptr(t)) {
            if (t == idle_task_ || t->magic != TaskControlBlock::TCB_MAGIC)
                continue;
            if (t->period_ticks > 0) {
                ++real_tasks;
                uint64_t wcet =
                    t->wcet_ticks > 0
                        ? t->wcet_ticks
                        : (t->sporadic_server ? t->sporadic_server->max_budget()
                                              : t->remaining_ticks);
                uint64_t util = (wcet * 1000000) / t->period_ticks;
                total_util += util;
            }
        }
        uint32_t bound = real_tasks <= LIU_LEYLAND_MAX_TASKS
                             ? LIU_LEYLAND_BOUNDS[real_tasks]
                             : LIU_LEYLAND_LIMIT;
        if (total_util > bound) {
            Logger::warn("Scheduler: task %d (prio=%d, period=%d) exceeds "
                         "Liu-Leyland bound (%d > %d) — overrun possible",
                         task.id, task.priority, task.period_ticks, total_util,
                         bound);
        }
    }
}

void Scheduler::remove_task(TaskControlBlock &task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    all_tasks_.remove(&task);
    deadline_list_.remove(&task);
    id_table_remove(&task);
    dequeue_ready(task);

    __atomic_store_n(&scheduler_save_rsp_to, nullptr, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);

    kernel::test::ResourceTracker::instance().track_task_remove();
}

bool Scheduler::unregister_task(TaskControlBlock &task) noexcept {
    if (!scheduler_lock_.try_lock()) {
        return false;
    }
    all_tasks_.remove(&task);
    deadline_list_.remove(&task);
    id_table_remove(&task);
    dequeue_ready(task);

    __atomic_store_n(&scheduler_save_rsp_to, nullptr, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);
    kernel::test::ResourceTracker::instance().track_task_remove();

    scheduler_lock_.unlock();
    return true;
}

// ---------------------------------------------------------------------------
// Current-task / lookup
// ---------------------------------------------------------------------------

TaskControlBlock *Scheduler::current_task() noexcept {
    return current_task_ptr_;
}

uint64_t Scheduler::task_count() noexcept {
    return all_tasks_.size();
}

uint64_t Scheduler::current_index() noexcept {
    uint64_t i = 0;
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (t == current_task_ptr_)
            return i;
        ++i;
    }
    return 0;
}

void Scheduler::set_current_index(uint64_t idx) noexcept {
    uint64_t i = 0;
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (i++ == idx) {
            current_task_ptr_ = t;
            return;
        }
    }
}

void Scheduler::set_current_task(TaskControlBlock *t) noexcept {
    if (t && t->magic == TaskControlBlock::TCB_MAGIC)
        current_task_ptr_ = t;
}

TaskControlBlock *Scheduler::task_at(uint64_t index) noexcept {
    // Index 0 is reserved for the idle (reaper) task, the root adoptive parent.
    // It is NOT necessarily the highest-priority bucket head in
    // AllTasksRegistry (idle is clamped to CONFIG_PRIORITY_CEILING and shares
    // that bucket with the deadline-monitor task), so return it explicitly
    // rather than via first_ptr().
    if (index == 0)
        return idle_task_;
    // Indices 1..N-1 map to the remaining (non-idle) tasks, by priority then
    // insertion order, so callers iterating 0..task_count()-1 still visit every
    // task exactly once (task_count() includes idle).
    uint64_t i = 0;
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (t == idle_task_)
            continue;
        if (i++ == index - 1)
            return t;
    }
    return nullptr;
}

TaskControlBlock *Scheduler::find_task(uint64_t id) noexcept {
    return id_table_find(id);
}

bool Scheduler::needs_switch() noexcept {
    if (all_tasks_.size() <= 1)
        return false;
    auto *current = current_task_ptr_;
    if (!current || current->magic != TaskControlBlock::TCB_MAGIC)
        return false;
    if (current == idle_task_)
        return true;

    // If the current task is not runnable (blocked/waiting/terminated), any
    // ready task must take over — otherwise a high-priority task that blocks
    // (e.g. inside IPC::send_sync) would keep "winning" the priority compare
    // against lower-priority ready peers and the scheduler would never switch
    // to them, deadlocking the handshake (kernel hang in the test suite).
    if (current->state != TaskState::READY &&
        current->state != TaskState::RUNNING)
        return true;

    uint64_t cur_eff = effective_priority(current);
    // O(1): check if any higher-priority task exists in the ready queue
    uint64_t highest_ready = ready_queue_.highest_ready_priority();
    return highest_ready > cur_eff;
}

TaskControlBlock *Scheduler::next_task() noexcept {
    if (all_tasks_.size() <= 1)
        return idle_task_;

    {
        auto *next = ready_queue_.dequeue_highest();
        while (next &&
               (next == current_task_ptr_ ||
                (next->state != TaskState::READY &&
                 next->state != TaskState::RUNNING))) {
            next = ready_queue_.dequeue_highest();
        }
        if (next) {
            return next;
        }
    }

    // Lazy rebuild
    ready_queue_.clear_all();
    {
        auto *cur = current_task_ptr_;
        for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
            if (t->magic != TaskControlBlock::TCB_MAGIC)
                continue;
            // Heal any in_ready_queue_ desync regardless of current state.  A
            // READY task whose flag is set but which is not physically in the
            // queue is silently dropped by enqueue() and next_task() can never
            // find it, wedging the scheduler in the idle loop (the `all` suite
            // hanging at the atomic context-switch tests).  The current task and
            // the idle/harness task are running (not queued) so we only clear
            // their flag, never re-enqueue them; every other READY or RUNNING
            // task is force-cleared and re-enqueued.  Re-enqueuing a RUNNING
            // (preempted) task mirrors what scheduler_on_context_switch does when
            // it switches away from it, which is what lets the physically-
            // executing test runner resume when current_task_ptr_ has drifted
            // onto a different (peer) TCB.  The idle/harness task is returned by
            // next_task() as the default when no other task is ready, so it must
            // never sit in the ready queue (at priority 0xFFFFFFFF it would
            // otherwise always preempt and wedge the suite).
            if (t == cur || t == idle_task_) {
                t->in_ready_queue_ = false;
                t->rq_priority_ = 0;
                continue;
            }
            if (t->state == TaskState::READY ||
                t->state == TaskState::RUNNING) {
                t->in_ready_queue_ = false;
                t->rq_priority_ = 0;
                ready_queue_.enqueue(*t, effective_priority(t));
                continue;
            }
            t->in_ready_queue_ = false;
            t->rq_priority_ = 0;
        }
    }

    {
        auto *next = ready_queue_.dequeue_highest();
        if (next) {
            return next;
        }
    }

    return idle_task_;
}

void Scheduler::set_current(TaskControlBlock &task) noexcept {
    auto *old = current_task_ptr_;
    if (old == &task) {
        __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
        __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);
        __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr,
                         __ATOMIC_RELEASE);
        return;
    }
    // Invariant: a task that is physically executing (current) must never sit
    // in the ready queue, or next_task() will re-select it.  The idle/harness
    // task runs at priority 0xFFFFFFFF, so leaving it queued makes it always
    // preempt — wedging the `all` suite (e.g. deadlocking at
    // ipc_send_sync_no_cli / test 536).  Remove the previous current so it is
    // no longer eligible; it stays resumable because next_task() returns it as
    // the idle/default task when no other task is ready.
    if (old && old->magic == TaskControlBlock::TCB_MAGIC && old->in_ready_queue_) {
        // remove() unlinks old from the intrusive list AND clears
        // in_ready_queue_/rq_priority_ itself.  Clearing in_ready_queue_ here
        // first would make TaskQueue::remove early-return (it guards on
        // !in_ready_queue_), leaving old physically linked with a stale
        // runq_next_/runq_prev_ — a dangling node that later corrupts the list
        // (pop_front dereferences a freed/reused TCB → #GP) once old is freed.
        ready_queue_.remove(*old, old->rq_priority_);
    }
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr,
                     __ATOMIC_RELEASE);
    current_task_ptr_ = &task;
}

void Scheduler::set_current_by_id(uint64_t id) noexcept {
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (t && t->magic == TaskControlBlock::TCB_MAGIC && t->id == id) {
            current_task_ptr_ = t;
            return;
        }
    }
    // Fallback: the id is not in the registry (TCB may have been unlinked or
    // current_task_ptr_ drifted).  Pin to the physically-running task by stack
    // ownership so a stale current_task_ptr_ can never persist and desync the
    // scheduler (which wedges the shell after `mkdir`).
    uint64_t cur_rsp{};
    asm volatile("mov %%rsp, %0" : "=r"(cur_rsp));
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (t && t->magic == TaskControlBlock::TCB_MAGIC && t->kernel_stack &&
            t->kernel_stack_top && cur_rsp >= reinterpret_cast<uint64_t>(t->kernel_stack) &&
            cur_rsp < t->kernel_stack_top) {
            current_task_ptr_ = t;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// O(1) task-ID→TCB hash table (open addressing, linear probing)
// ---------------------------------------------------------------------------

uint64_t Scheduler::id_table_probe(uint64_t id) {
    return id & ID_TABLE_MASK;
}

void Scheduler::id_table_insert(uint64_t id, TaskControlBlock *tcb) {
#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-infinite-loop"
#endif
    uint64_t idx = id_table_probe(id);
    while (id_table_[idx] != nullptr && id_table_[idx] != ID_TOMBSTONE) {
        idx = (idx + 1) & ID_TABLE_MASK;
    }
    id_table_[idx] = tcb;
#ifndef __clang__
#pragma GCC diagnostic pop
#endif
}

uint64_t Scheduler::alloc_id() noexcept {
    return next_task_id_++;
}

void Scheduler::reset_next_task_id(uint64_t id) noexcept {
    next_task_id_ = id;
}

void Scheduler::id_table_remove(TaskControlBlock *task) {
    uint64_t idx = id_table_probe(task->id);
    for (uint64_t probe = 0; probe < ID_TABLE_SIZE; ++probe) {
        if (id_table_[idx] == nullptr)
            return;
        if (id_table_[idx] != ID_TOMBSTONE && id_table_[idx] == task) {
            id_table_[idx] = ID_TOMBSTONE;
            return;
        }
        idx = (idx + 1) & ID_TABLE_MASK;
    }
}

TaskControlBlock *Scheduler::id_table_find(uint64_t id) {
    uint64_t idx = id_table_probe(id);
    while (id_table_[idx] != nullptr) {
        if (id_table_[idx] != ID_TOMBSTONE && id_table_[idx]->id == id) {
            return id_table_[idx];
        }
        idx = (idx + 1) & ID_TABLE_MASK;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// on_tick — timer tick handler
// ---------------------------------------------------------------------------

void Scheduler::on_tick() noexcept {
    uint64_t current_tick = arch::Timer::ticks();

    if (!preempt_enabled_) {
        return;
    }

    bool lock_acquired = scheduler_lock_.try_lock();
    if (lock_acquired) {
#if defined(CONFIG_DEBUG_IPC_SCHED)
        // H2 wedge detector: scan every task for a READY/RUNNING task whose
        // in_ready_queue_ flag says it is in the runq but which is NOT
        // physically linked in any priority queue. Such a desync is normally
        // healed by next_task()'s rebuild, but that rebuild is only reached
        // when rate_monotonic_schedule is NOT gated by a stale pending switch.
        // If the orphan persists while save_rsp_to != 0, we have the
        // self-deadlock signature.  Throttled to avoid serial flood.
        {
            uint64_t phys_rsp{};
            asm volatile("mov %%rsp, %0" : "=r"(phys_rsp));
            const uint64_t save_to = reinterpret_cast<uint64_t>(
                __atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE));
            bool orphan_found = false;
            for (auto *t = all_tasks_.first_ptr(); t;
                 t = all_tasks_.next_ptr(t)) {
                if (t->magic != TaskControlBlock::TCB_MAGIC)
                    continue;
                if (t == idle_task_ || t == current_task_ptr_)
                    continue;
                if (t->state != TaskState::READY &&
                    t->state != TaskState::RUNNING)
                    continue;
                if (!t->in_ready_queue_)
                    continue;
                bool phys = false;
                for (uint64_t p = 0;
                     p <= CONFIG_PRIORITY_CEILING && !phys; ++p) {
                    if (ready_queue_.queue(p).contains(*t))
                        phys = true;
                }
                if (!phys) {
                    orphan_found = true;
                    if (s_wedge_emitted_ < 8) {
                        IPC_SCHED_TRACE(
                            "[WEDGE]", "orphan=", t->id, "st=",
                            static_cast<uint64_t>(t->state), "inrq=",
                            t->in_ready_queue_ ? 1u : 0u, "phys=", 0u);
                    }
                }
            }
            if (orphan_found && s_wedge_emitted_ < 8) {
                ++s_wedge_emitted_;
                char wb[128];
                int wp = 0;
                kernel::debug::fmt_str(wb, wp, "[WEDGE] save_to=");
                wp = kernel::debug::fmt_u64(wb, wp, save_to);
                kernel::debug::fmt_str(wb, wp, " stale_ticks=");
                wp = kernel::debug::fmt_u64(wb, wp, 0u);
                kernel::debug::fmt_str(wb, wp, " cur=");
                wp = kernel::debug::fmt_u64(
                    wb, wp,
                    (current_task_ptr_
                         ? static_cast<uint64_t>(current_task_ptr_->id)
                         : 0u));
                kernel::debug::fmt_str(wb, wp, " bm_lo=");
                wp = kernel::debug::fmt_u64(
                    wb, wp, ready_queue_.bitmap().raw_lo());
                kernel::debug::fmt_str(wb, wp, " bm_hi=");
                wp = kernel::debug::fmt_u64(
                    wb, wp, ready_queue_.bitmap().raw_hi());
                kernel::debug::fmt_str(wb, wp, " physrsp=");
                wp = kernel::debug::fmt_u64(wb, wp, phys_rsp);
                wb[wp] = 0;
                kernel::debug::trace(wb);
            }
        }
#endif
#if CONFIG_DEADLINE_MONITOR_TASK
        if (!s_test_active_) {
            __atomic_store_n(&s_scan_requested_, 1, __ATOMIC_RELEASE);
            if (s_monitor_task_ &&
                s_monitor_task_->state == TaskState::BLOCKED) {
                s_monitor_task_->state = TaskState::READY;
                enqueue_ready(*s_monitor_task_);
            }
        }
#else
#if CONFIG_DEADLINE_MISS_DETECTION
        // O(1) deadline scan via DeadlineList
        while (auto *task = deadline_list_.pop_earliest_if_expired()) {
            if (task->sporadic_server) {
                task->ss_state_on_deadline_miss =
                    static_cast<uint8_t>(task->sporadic_server->state());
                task->ss_budget_on_deadline_miss =
                    task->sporadic_server->remaining_budget();
            }
            task->deadline_missed = true;
            ++task->deadline_miss_count;
            deadline_miss_handler(task,
                                  arch::Timer::ticks() - task->deadline_ticks);
        }
#endif
#endif // CONFIG_DEADLINE_MONITOR_TASK

        // Accounting, WCET, alarms — common to both paths
        for (auto *task = all_tasks_.first_ptr(); task;
             task = all_tasks_.next_ptr(task)) {
            if (task->magic != TaskControlBlock::TCB_MAGIC)
                continue;
            if (task->state == TaskState::TERMINATED)
                continue;

            if (task->state == TaskState::RUNNING ||
                task->state == TaskState::READY) {
                ++task->executed_ticks;
                uint64_t prev_rem = task->remaining_ticks;
                if (task->remaining_ticks > 0)
                    --task->remaining_ticks;
                if (prev_rem == 0 && task->period_ticks > 0) {
                    task->remaining_ticks = task->period_ticks;
#if CONFIG_DEADLINE_MISS_DETECTION && !CONFIG_DEADLINE_MONITOR_TASK
                    task->deadline_ticks += task->period_ticks;
                    task->deadline_missed = false;
                    if (task->deadline_ticks > 0) {
                        deadline_list_.insert(task);
                    }
#if CONFIG_WCET_OVERRUN_DETECTION
                    task->wcet_overrun_fired = false;
#endif
#endif
                }
            }

#if CONFIG_WCET_OVERRUN_DETECTION
            if (task->wcet_ticks > 0 && !task->wcet_overrun_fired &&
                task->executed_ticks > task->wcet_ticks) {
                task->wcet_overrun_fired = true;
                wcet_overrun_handler(task,
                                     task->executed_ticks - task->wcet_ticks);
            }
#endif

            if (task->alarm_armed) {
                if (task->alarm_ticks > 0)
                    --task->alarm_ticks;
                if (task->alarm_ticks == 0) {
                    task->alarm_armed = false;
                    task->pending_signals |=
                        (1ULL << static_cast<uint64_t>(Signal::SIGALRM));
                }
            }
        }
#if !CONFIG_DEADLINE_MONITOR_TASK
        __atomic_fetch_add(&deadline_detection_integrity, 1, __ATOMIC_RELEASE);
#endif
        scheduler_lock_.unlock();
    }

    if (s_deferred_kill_count > 0)
        Scheduler::process_deferred_kills();

    // Sporadic Server budget management
    {
        auto *cur = current_task_ptr_;
        uint64_t found = 0;
        for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
            if (found >= sporadic_task_count_)
                break;
            if (t->magic != TaskControlBlock::TCB_MAGIC)
                continue;
            if (t->sporadic_server) {
                found++;
                if (reinterpret_cast<uint64_t>(t->sporadic_server) ==
                    0xFFFFFFFFFFFFFFFFULL) {
                    Logger::raw_write("[BUG] on_tick: sporadic_server=-1\n");
                    continue;
                }
                t->sporadic_server->process_replenishments(current_tick);
                if (t == cur && t->sporadic_server->is_active()) {
                    if (!t->sporadic_server->consume(current_tick)) {
#if CONFIG_SPORADIC_SERVER_EXHAUSTION_IS_DEADLINE
                        if (!t->deadline_missed) {
                            t->ss_state_on_deadline_miss = static_cast<uint8_t>(
                                t->sporadic_server->state());
                            t->ss_budget_on_deadline_miss =
                                t->sporadic_server->remaining_budget();
                            t->deadline_missed = true;
                            ++t->deadline_miss_count;
                            deadline_miss_handler(t, 0);
                        }
#endif
                        uint64_t rsp{};
                        asm volatile("mov %%rsp, %0" : "=r"(rsp));
                        uint64_t base =
                            reinterpret_cast<uint64_t>(cur->kernel_stack);
                        if (cur->kernel_stack && cur->kernel_stack_top &&
                            rsp >= base && rsp < cur->kernel_stack_top) {
                            Scheduler::reschedule();
                        }
                    }
                }
            }
        }
    }

    static uint64_t tick_counter = 0;
    ++tick_counter;
    if (tick_counter % 100 == 0) {
        reap_orphans();
        if (lock_acquired) {
            daemon::restart_stale_daemons();
        }
    }

    rate_monotonic_schedule();
}

// ---------------------------------------------------------------------------
// reap_orphans
// ---------------------------------------------------------------------------

void Scheduler::reap_orphans() noexcept {
    if (s_reap_in_progress)
        return;
    s_reap_in_progress = true;

    auto *current = current_task_ptr_;
    auto *init_task = idle_task_;
    TaskControlBlock *new_idle = nullptr;

    // Collect reapable tasks first (can't mutate all_tasks_ during iteration)
    static constexpr uint64_t MAX_REAP = 64;
    TaskControlBlock *to_reap[MAX_REAP];
    uint64_t num_to_reap = 0;

    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (num_to_reap >= MAX_REAP)
            break;
        if (t->magic != TaskControlBlock::TCB_MAGIC)
            continue;
        if (t->state != TaskState::TERMINATED)
            continue;
        if (t != idle_task_ && t == current)
            continue;

        // Adopt children to init_task
        if (init_task && init_task != t) {
            if (t->first_child) {
                auto *child = t->first_child;
                t->first_child = nullptr;
                if (t->num_children > 0)
                    t->num_children = 0;
                while (child) {
                    auto *next = child->next_sibling;
                    child->prev_sibling = nullptr;
                    child->next_sibling = nullptr;
                    child->parent_id = 0;
                    init_task->add_child(child);
                    child = next;
                }
            }
            for (TaskIter it(0);;) {
                auto *c = it.next(t);
                if (!c)
                    break;
                if (c == init_task)
                    continue;
                if (c->parent_id == t->id) {
                    c->parent_id = 0;
                    if (t->num_children > 0)
                        --t->num_children;
                    init_task->add_child(c);
                }
            }
        }

        // Determine if reapable
        bool can_reap = false;
        if (t->parent_id == 0) {
            can_reap = true;
        } else if (init_task && t->parent_id == init_task->id &&
                   init_task->state == TaskState::RUNNING) {
            can_reap = true;
        } else {
            bool parent_found = false;
            for (TaskIter it(0);;) {
                auto *p = it.next();
                if (!p)
                    break;
                if (p->id == t->parent_id) {
                    parent_found = true;
                    bool terminated = (p->state == TaskState::TERMINATED);
                    bool waiting_for_this =
                        (p->waiting_child_pid == t->id);
                    bool waiting_for_any =
                        (p->waiting_child_pid ==
                         static_cast<uint64_t>(-1));
                    // Reap unless the (live) parent is blocked specifically in
                    // waitpid for *this* child (deferred reap) or for *any*
                    // child (sentinel -1).
                    can_reap =
                        terminated || (!waiting_for_this && !waiting_for_any);
                    break;
                }
            }
            if (!parent_found)
                can_reap = true;
        }

        if (!can_reap)
            continue;

        if (!t->page_table_shared_) {
            bool has_sharing_child = false;
            for (TaskIter it(0);;) {
                auto *c = it.next(t);
                if (!c)
                    break;
                if (c == current)
                    continue;
                if (c->parent_id == t->id && c->page_table_shared_) {
                    has_sharing_child = true;
                    break;
                }
            }
            if (has_sharing_child)
                continue;
        }

        to_reap[num_to_reap++] = t;
    }

    // Destroy reaped tasks
    for (uint64_t ri = 0; ri < num_to_reap; ++ri) {
        auto *t = to_reap[ri];
        dequeue_ready(*t);
        all_tasks_.remove(t);
        id_table_remove(t);
        deadline_list_.remove(t);
        if (t == idle_task_) {
            auto *created = TaskControlBlock::create(
                kernel::integrity::idle_task_main, 0, 0xFFFFFFFF);
            created->state = TaskState::READY;
            if (!suppress_terminated_log_)
                Logger::info("Scheduler: task '%s' (ID=%u) terminated", t->name,
                             t->id);
            t->cleanup();
            MemPool::free(t);
            new_idle = created;
        } else {
            if (!suppress_terminated_log_)
                Logger::info("Scheduler: task '%s' (ID=%u) terminated", t->name,
                             t->id);
            t->cleanup();
            MemPool::free(t);
        }
    }

    // If idle was recreated, register it
    if (new_idle) {
        all_tasks_.append(new_idle);
        idle_task_ = new_idle;
        id_table_insert(new_idle->id, new_idle);
    }

    // Restore current_task_ptr_
    if (current == idle_task_ && new_idle) {
        current_task_ptr_ = new_idle;
    } else {
        // Verify current_task_ptr_ is still valid
        if (current_task_ptr_ &&
            current_task_ptr_->magic != TaskControlBlock::TCB_MAGIC) {
            current_task_ptr_ = all_tasks_.first_ptr();
        }
    }

    s_reap_in_progress = false;
}

// ---------------------------------------------------------------------------
// cleanup_test_tasks
// ---------------------------------------------------------------------------

void Scheduler::cleanup_test_tasks() noexcept {
    // The task currently executing this routine is still running on its own
    // kernel stack. Freeing it here (via t->cleanup()) would reclaim the very
    // stack we are executing on — a use-after-free that corrupts the return
    // path with poison and faults. reap_orphans() already skips t == current;
    // mirror that guard here.
    TaskControlBlock *const running = current_task_ptr_;

    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (t == idle_task_ || t == running)
            continue;
        if (t->magic == TaskControlBlock::TCB_MAGIC &&
            t->state != TaskState::TERMINATED) {
            t->state = TaskState::TERMINATED;
            t->exit_code = 0;
            wake_waiting_parent(*t);
        }
    }
    reap_orphans();

    // Remove remaining non-idle tasks
    static constexpr uint64_t MAX_CLEANUP = 64;
    TaskControlBlock *to_remove[MAX_CLEANUP];
    uint64_t num = 0;
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (num >= MAX_CLEANUP)
            break;
        if (t == idle_task_ || t == running)
            continue;
        if (t->magic == TaskControlBlock::TCB_MAGIC) {
            to_remove[num++] = t;
        }
    }
    for (uint64_t i = 0; i < num; ++i) {
        auto *t = to_remove[i];
        // Unlink from every scheduler list before freeing.  These tasks
        // survived reap_orphans() (their parent is still alive) but may still
        // be physically linked in the ready queue / deadline list / id table;
        // freeing without unlinking leaves a dangling node that a later
        // ready-queue walk dereferences → #GP.
        Scheduler::dequeue_ready(*t);
        deadline_list_.remove(t);
        id_table_remove(t);
        all_tasks_.remove(t);
        t->cleanup();
        MemPool::free(t);
    }

    for (uint64_t i = 0; i < ID_TABLE_SIZE; ++i)
        id_table_[i] = nullptr;
    all_tasks_.clear();
    all_tasks_.append(idle_task_);
    id_table_insert(idle_task_->id, idle_task_);
    // Keep the running task in the live set (its stack is still in use); do
    // not drop it into limbo. If it is idle it is already appended above.
    if (running && running != idle_task_) {
        all_tasks_.append(running);
        id_table_insert(running->id, running);
    }
    current_task_ptr_ = idle_task_;
    ready_queue_.reset();
}

// ---------------------------------------------------------------------------
// Corruption / validation helpers
// ---------------------------------------------------------------------------

static void report_corruption(const char *label) {
    (void)label;
    __atomic_fetch_add(&scheduler_corruption_count, 1, __ATOMIC_RELEASE);
#ifdef CONFIG_DEBUG
    ENSURE(false && "scheduler corruption detected — see log above");
#endif
}

static bool rsp_in_stack_range(uint64_t rsp, const TaskControlBlock *t,
                               const char *label) {
    auto base = reinterpret_cast<uint64_t>(t->kernel_stack);
    auto top = t->kernel_stack_top;
    if (rsp >= base && rsp <= top)
        return true;
    if (rsp < base)
        return false;
    Logger::raw_write("[SCHED] ");
    Logger::raw_write(label);
    Logger::raw_write(": task id=");
    Logger::print_dec(t->id);
    Logger::raw_write(" rsp=0x");
    Logger::print_hex(rsp);
    Logger::raw_write(" above stack top 0x");
    Logger::print_hex(top);
    Logger::raw_write("\n");
    return false;
}

static bool validate_switch(TaskControlBlock *current, TaskControlBlock *next,
                            const char *label) {
    if (!current) {
        Logger::raw_write("[SCHED] ");
        Logger::raw_write(label);
        Logger::raw_write(": current=null\n");
        return false;
    }
    if (!next) {
        Logger::raw_write("[SCHED] ");
        Logger::raw_write(label);
        Logger::raw_write(": next=null\n");
        return false;
    }
    auto caddr = reinterpret_cast<uint64_t>(current);
    auto naddr = reinterpret_cast<uint64_t>(next);
    if (caddr < 0xFFFF800000000000ULL) {
        Logger::raw_write("[SCHED] ");
        Logger::raw_write(label);
        Logger::raw_write(": current low 0x");
        Logger::print_hex(caddr);
        Logger::raw_write("\n");
        return false;
    }
    if (naddr < 0xFFFF800000000000ULL) {
        Logger::raw_write("[SCHED] ");
        Logger::raw_write(label);
        Logger::raw_write(": next low 0x");
        Logger::print_hex(naddr);
        Logger::raw_write("\n");
        return false;
    }
    if (current->magic != TaskControlBlock::TCB_MAGIC) {
        Logger::raw_write("[SCHED] ");
        Logger::raw_write(label);
        Logger::raw_write(": current magic=0x");
        Logger::print_hex(current->magic);
        Logger::raw_write("\n");
        return false;
    }
    if (next->magic != TaskControlBlock::TCB_MAGIC) {
        Logger::raw_write("[SCHED] ");
        Logger::raw_write(label);
        Logger::raw_write(": next magic=0x");
        Logger::print_hex(next->magic);
        Logger::raw_write("\n");
        return false;
    }
    if (next != Scheduler::get_idle_task() &&
        !rsp_in_stack_range(TASK_STACK_PTR(next), next, label)) {
        if (TASK_STACK_PTR(next) <
            reinterpret_cast<uint64_t>(next->kernel_stack)) {
            return true;
        }
        Logger::raw_write("[SCHED] ");
        Logger::raw_write(label);
        Logger::raw_write(": current id=");
        Logger::print_dec(current->id);
        Logger::raw_write(" rsp=0x");
        Logger::print_hex(TASK_STACK_PTR(current));
        Logger::raw_write(" state=");
        Logger::print_dec(static_cast<uint64_t>(current->state));
        Logger::raw_write(" kstack=[0x");
        Logger::print_hex(reinterpret_cast<uint64_t>(current->kernel_stack));
        Logger::raw_write("-0x");
        Logger::print_hex(current->kernel_stack_top);
        Logger::raw_write("]\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// cleanup_zombies
// ---------------------------------------------------------------------------

void Scheduler::cleanup_zombies() noexcept {
    if (s_reap_in_progress)
        return;
    auto *shell = shell_task_ptr_;
    if (!shell ||
        (reinterpret_cast<uint64_t>(shell) & 0xFFFF800000000000ULL) !=
            0xFFFF800000000000ULL ||
        shell->magic != TaskControlBlock::TCB_MAGIC) {
        shell = current_task_ptr_;
    }
    uint64_t vfsd_pid = vfsd::get_vfsd_pid();
    uint64_t iocd_pid = iocd::get_iocd_pid();

    static constexpr uint64_t MAX_ZOMBIES = 64;
    TaskControlBlock *zombies[MAX_ZOMBIES];
    uint64_t num_zombies = 0;
    for (auto *t = all_tasks_.first_ptr(); t && num_zombies < MAX_ZOMBIES;
         t = all_tasks_.next_ptr(t)) {
        if (t->magic != TaskControlBlock::TCB_MAGIC)
            continue;
        if (t == idle_task_ || t == shell || t->id == vfsd_pid ||
            t->id == iocd_pid) {
            continue;
        }
        // Only consider tasks that are already terminated – otherwise they will
        // be handled later by reap_orphans(). This prevents double‑removal and
        // underflow.
        if (t->state != TaskState::TERMINATED)
            continue;
        zombies[num_zombies++] = t;
    }

    for (uint64_t i = 0; i < num_zombies; ++i) {
        auto *t = zombies[i];
        // Always unlink from the ready queue before freeing.  A task can be
        // physically linked in the queue while in a non-READY state (e.g. a
        // RUNNING task left linked by a current_task_ptr_ desync, or a
        // TERMINATED task not yet reaped).  Gating the dequeue on
        // state==READY left such a node linked, so MemPool::free() freed a
        // still-linked TCB and the next ready-queue walk dereferenced a
        // freed/reused node → #GP.  dequeue_ready() is a no-op when the task
        // is not in the queue, so unlinking unconditionally is safe.
        Scheduler::dequeue_ready(*t);
        t->cleanup();
        all_tasks_.remove(t);
        deadline_list_.remove(t);
        id_table_remove(t);
        MemPool::free(t);
    }
}

// ---------------------------------------------------------------------------
// switch_to_task
// ---------------------------------------------------------------------------

static void switch_to_task(TaskControlBlock *current, TaskControlBlock *next,
                           sync::SpinLock *held_lock = nullptr) {
    auto release_lock = [&]() {
        if (held_lock) {
            held_lock->unlock();
            held_lock = nullptr;
        }
    };

    if (!validate_switch(current, next, "switch")) {
        report_corruption("switch");
        release_lock();
        return;
    }
    if (next->state != TaskState::READY && next->state != TaskState::RUNNING) {
        report_corruption("switch next state");
        release_lock();
        return;
    }
    if (current == next) {
        release_lock();
        return;
    }

    uint64_t *save_target = &TASK_STACK_PTR(current);
    {
        uint64_t cur_rsp{};
        asm volatile("mov %%rsp, %0" : "=r"(cur_rsp));
        TaskControlBlock *owner = nullptr;
#ifndef CONFIG_DEBUG
        uint64_t base = reinterpret_cast<uint64_t>(current->kernel_stack);
        // Release builds: only resolve the true RSP owner when the live RSP is
        // NOT inside `current`'s own kernel stack (the rare drift case).  This
        // keeps the common context-switch path O(1) with no per-switch task scan.
        if (!current->kernel_stack || !current->kernel_stack_top ||
            cur_rsp < base || cur_rsp >= current->kernel_stack_top) {
            for (uint64_t ti = 0; ti < Scheduler::task_count(); ++ti) {
                auto *tt = Scheduler::task_at(ti);
                if (!tt || tt->magic != TaskControlBlock::TCB_MAGIC)
                    continue;
                uint64_t tb = reinterpret_cast<uint64_t>(tt->kernel_stack);
                if (tt->kernel_stack && tt->kernel_stack_top &&
                    cur_rsp >= tb && cur_rsp < tt->kernel_stack_top) {
                    owner = tt;
                    break;
                }
            }
        }
#else
        // Debug builds: always resolve the physically-running task by stack
        // ownership.  current_task_ptr_ can drift onto a peer TCB (context-switch
        // bookkeeping desync) while the CPU is actually executing on another
        // task's kernel stack; saving the live CPU state into the wrong TCB
        // corrupts that peer's context.rsp and wedges the scheduler.  Always pin
        // `current` to the true RSP owner so the save lands in the real runner.
        // The unconditional O(n) scan is acceptable only under CONFIG_DEBUG.
        for (uint64_t ti = 0; ti < Scheduler::task_count(); ++ti) {
            auto *tt = Scheduler::task_at(ti);
            if (!tt || tt->magic != TaskControlBlock::TCB_MAGIC)
                continue;
            uint64_t tb = reinterpret_cast<uint64_t>(tt->kernel_stack);
            if (tt->kernel_stack && tt->kernel_stack_top &&
                cur_rsp >= tb && cur_rsp < tt->kernel_stack_top) {
                owner = tt;
                break;
            }
        }
#endif
        if (owner && owner != current) {
            current = owner;
            Scheduler::set_current(*owner);
        }
        save_target = &TASK_STACK_PTR(current);
    }

#ifdef CONFIG_DEBUG
    {
        auto &ring = current->debug_switch_ring;
        auto &idx = current->debug_switch_idx;
        auto &rec = ring[idx % TaskControlBlock::DEBUG_SWITCH_RING_SIZE];
        rec.entry_addr =
            reinterpret_cast<uint64_t>(__builtin_return_address(0));
#if defined(CONFIG_ARCH_X86_64)
        rec.exit_rip = current->context.rip;
#elif defined(CONFIG_ARCH_AARCH64)
        rec.exit_rip = current->context.elr_el1;
#endif
        rec.regs = current->context;
        rec.thread_id = current->id;
        rec.consumed_ticks = current->executed_ticks;
        ++idx;
    }
#endif
    {
        uint64_t nsp = TASK_STACK_PTR(next);
        uint64_t nbase = reinterpret_cast<uint64_t>(next->kernel_stack);
        uint64_t npg = reinterpret_cast<uint64_t>(next->page_table_);
        bool bad = (!nsp || nsp < nbase || nsp >= next->kernel_stack_top ||
                    (npg != 0 && (npg & 0xFFF) != 0));
        uint64_t f_rflags = 0;
        if (!bad) {
            const uint64_t *f = reinterpret_cast<const uint64_t *>(nsp);
            // The iret frame sits above the saved register frame.  Two valid
            // layouts exist: a freshly-created task (task.cpp builds rip first)
            // and a task saved by isr_common (CPU order: ss first).  Both place
            // rflags at +152; rip/cs/rsp/ss are swapped between the two
            // orderings, so validate either one instead of assuming a single
            // fixed order (the wrong order made valid RUN-task frames look
            // corrupt and dropped their switch — wedging the `all` suite).
            uint64_t rip_a = f[136 / 8], cs_a = f[144 / 8], rsp_a = f[160 / 8],
                     ss_a = f[168 / 8]; // created order (rip first)
            uint64_t rip_b = f[168 / 8], cs_b = f[160 / 8], rsp_b = f[144 / 8],
                     ss_b = f[136 / 8]; // isr_common/CPU order (ss first)
            f_rflags = f[152 / 8];
            auto frame_ok = [&](uint64_t rip, uint64_t cs, uint64_t rsp,
                               uint64_t ss) -> bool {
                bool ring0 = (cs == 0x8);
                bool ring3 = (cs == 0x1B);
                if (rip == 0 || (!ring0 && !ring3))
                    return false;
                if ((ring0 || ring3) && (f_rflags & 0x2) == 0)
                    return false;
                if (ring3 && (ss != 0x23 || rsp == 0))
                    return false;
                return true;
            };
            if (!frame_ok(rip_a, cs_a, rsp_a, ss_a) &&
                !frame_ok(rip_b, cs_b, rsp_b, ss_b))
                bad = true;
        }
        if (bad) {
            // ---- Dispatch guard ----
            // Never iretq into a task whose iret frame is invalid.  This can
            // happen when `next` is the physically-running task but
            // current_task_ptr_ has drifted onto a peer TCB (the running task
            // was never switched out, so its stack slot at context.rsp+136 is
            // live data, not a CPU-written iret frame).  In that case treat it
            // as a no-op self-switch: keep the physical runner going and correct
            // current_task_ptr_.  Otherwise skip the dispatch and let the
            // current task continue (the bad task stays queued and is retried
            // once it has a real iret frame).
            uint64_t phys_rsp{};
            asm volatile("mov %%rsp, %0" : "=r"(phys_rsp));
            uint64_t nb = reinterpret_cast<uint64_t>(next->kernel_stack);
            bool next_is_runner =
                (next == current) ||
                (next->kernel_stack && next->kernel_stack_top &&
                 phys_rsp >= nb && phys_rsp < next->kernel_stack_top);
            bool next_is_running = (next->state == TaskState::RUNNING);
            if (next_is_runner || next_is_running) {
                // `next` is the physically-running task but it was left in the
                // ready queue (e.g. its snapshot state was READY, or current_
                // task_ptr_ had drifted).  Promote it to the current RUNNING
                // task and remove it from the queue so next_task() does not
                // exclude it as `current` and then fall through to idle (which
                // would permanently starve the live runner).  A RUNNING task has
                // no valid iret frame (its stack slot at context.rsp+136 is live
                // data, so we must NOT iretq into it — just keep it running.
                Scheduler::set_current(*next);
                next->state = TaskState::RUNNING;
                next->in_ready_queue_ = false;
                next->rq_priority_ = 0;
            }
            release_lock();
            return; // do not set scheduler_load_rsp_from -> no switch
        }
    }
    __atomic_store_n(&scheduler_load_rsp_from, TASK_STACK_PTR(next),
                     __ATOMIC_RELEASE);
    if (next->page_table_) {
        __atomic_store_n(&scheduler_load_cr3_from, next->page_table_,
                         __ATOMIC_RELEASE);
#if defined(CONFIG_DEBUG_IPC_SCHED)
        IPC_SCHED_TRACE("[SW]", "next=", next->id, "cr3=",
                        next->page_table_, "rsp=", TASK_STACK_PTR(next), "x=",
                        0u);
#endif
#if defined(CONFIG_ARCH_X86_64)
        arch::GDT::set_tss_rsp0(next->kernel_stack_top);
#endif
    } else {
        __atomic_store_n(&scheduler_load_cr3_from, VMM::get_kernel_pml4(),
                         __ATOMIC_RELEASE);
    }

    if (current->state == TaskState::RUNNING) {
        current->state = TaskState::READY;
        Scheduler::enqueue_ready(*current);
    }
    next->state = TaskState::RUNNING;

    {
        arch::IrqGuard ig{};
        release_lock();
        __atomic_store_n(&scheduler_next_task_id, next->id, __ATOMIC_RELEASE);
        __atomic_store_n(&scheduler_save_rsp_to, save_target, __ATOMIC_RELEASE);

        uint64_t cr0 = arch::read_cr0();
        cr0 |= (1ULL << 3);
        arch::write_cr0(cr0);
    }
}

/// @brief Corrects current_task_ptr_ to the task physically executing on the
///        live kernel stack.  Some test helpers (e.g. yield_as) or context
///        switches can leave current_task_ptr_ pointed at a peer TCB while the
///        CPU is actually running on another task's stack; saving the live
///        register state into the wrong TCB corrupts it and desyncs the
///        scheduler (the `all` suite wedging/hanging).  Call before selecting
///        the next task so saves land in the real running task.  The
///        set_current() invariant (the running task is never in the ready
// ---------------------------------------------------------------------------
// Rate-monotonic schedule / reschedule
// ---------------------------------------------------------------------------

void Scheduler::rate_monotonic_schedule() noexcept {
    if (all_tasks_.size() <= 1)
        return;

    if (!scheduler_lock_.try_lock()) {
        return;
    }

    if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != 0) {
        scheduler_lock_.unlock();
        return;
    }

    auto *current = current_task_ptr_;
    if (!current || current->magic != TaskControlBlock::TCB_MAGIC) {
        scheduler_lock_.unlock();
        return;
    }

    current = current_task_ptr_;

    auto *next = next_task();
    // Never switch a live RUNNING current task to idle: that would idle-loop
    // forever and permanently starve the live task (e.g. the test harness
    // running the `all` suite between tests, when it is the only runnable
    // task).  If no other task is ready, keep running current.
    if (next && next != current &&
        !(next == idle_task_ && current->state == TaskState::RUNNING)) {
        switch_to_task(current, next, nullptr);
    }

    scheduler_lock_.unlock();
}

void Scheduler::reschedule() noexcept {
    scheduler_lock_.lock();
    if (all_tasks_.size() <= 1) {
        scheduler_lock_.unlock();
        return;
    }

    auto *current = current_task_ptr_;
    if (!current || current->magic != TaskControlBlock::TCB_MAGIC) {
        scheduler_lock_.unlock();
        return;
    }

    // A switch may already be pending (deferred) from a prior reschedule()
    // whose timer ISR has not yet applied it (e.g. when the caller disabled
    // IRQs via yield_as() between the previous reschedule and now).  Do NOT
    // skip — the latest reschedule() expresses the most recent scheduling
    // intent and must override the stale pending switch; the ISR applies
    // whatever was written last.  Consuming the stale pending slot here lets
    // the new switch be set up unconditionally.
    if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != nullptr) {
        __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&scheduler_next_task_id, UINT64_MAX, __ATOMIC_RELEASE);
    }

    auto *next = next_task();
    if (!next || next == current) {
        scheduler_lock_.unlock();
        return;
    }

    // Keep a live RUNNING current task running instead of idling it.
    if (next == idle_task_ && current->state == TaskState::RUNNING) {
        scheduler_lock_.unlock();
        return;
    }

    if (next->state != TaskState::READY && next->state != TaskState::RUNNING) {
        scheduler_lock_.unlock();
        return;
    }

    switch_to_task(current, next, &scheduler_lock_);
}

// ---------------------------------------------------------------------------
// Test-isolation helpers
// ---------------------------------------------------------------------------

void Scheduler::capture_state(TaskControlBlock **tasks_out,
                              TaskControlBlock **id_table_out,
                              uint64_t &task_count_out,
                              uint64_t &current_idx_out, uint64_t &next_id_out,
                              TaskControlBlock *&idle_out, bool &preempt_out,
                              uint64_t *rq_bitmap_hi, uint64_t *rq_bitmap_lo,
                              uint64_t *sporadic_count_out) {
    all_tasks_.capture(tasks_out, MAX_TASKS);
    __builtin_memcpy(static_cast<void *>(id_table_out),
                     static_cast<const void *>(id_table_),
                     sizeof(TaskControlBlock *) * ID_TABLE_SIZE);
    task_count_out = all_tasks_.size();
    current_idx_out = current_index();
    next_id_out = next_task_id_;
    idle_out = idle_task_;
    preempt_out = preempt_enabled_;
    if (rq_bitmap_hi)
        *rq_bitmap_hi = ready_queue_.bitmap().raw_hi();
    if (rq_bitmap_lo)
        *rq_bitmap_lo = ready_queue_.bitmap().raw_lo();
    if (sporadic_count_out)
        *sporadic_count_out = sporadic_task_count_;
}

void Scheduler::capture_rqpod(ReadyQueuePOD &out) noexcept {
    ready_queue_.capture_pod(out);
}

void Scheduler::restore_rqpod(const ReadyQueuePOD &src) noexcept {
    ready_queue_.restore_pod(src);
}

void Scheduler::reset_ready_queue() noexcept {
    ready_queue_.reset();
}

void Scheduler::rebuild_ready_queue() noexcept {
    ready_queue_.reset();
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (t->magic != TaskControlBlock::TCB_MAGIC)
            continue;
        if (t->state == TaskState::READY) {
            // Clear the flag first: enqueue() (TaskQueue::push_back) refuses to
            // re-add a node whose flag is already set, which would otherwise
            // leave the task out of the physical queue while the flag wrongly
            // claims membership.
            t->in_ready_queue_ = false;
            ready_queue_.enqueue(*t, effective_priority(t));
        } else {
            t->in_ready_queue_ = false;
            t->runq_next_ = nullptr;
            t->runq_prev_ = nullptr;
        }
    }
}

void Scheduler::restore_state(TaskControlBlock *const *tasks_in,
                              TaskControlBlock *const *id_table_in,
                              uint64_t task_count_in, uint64_t current_idx_in,
                              uint64_t next_id_in, TaskControlBlock *idle_in,
                              bool preempt_in, uint64_t rq_bitmap_hi,
                              uint64_t rq_bitmap_lo,
                              uint64_t sporadic_count_in) {
    all_tasks_.restore(tasks_in, task_count_in);
    __builtin_memcpy(static_cast<void *>(id_table_),
                     static_cast<const void *>(id_table_in),
                     sizeof(TaskControlBlock *) * ID_TABLE_SIZE);
    next_task_id_ = next_id_in;
    idle_task_ = idle_in;
    preempt_enabled_ = preempt_in;
    sporadic_task_count_ = sporadic_count_in;

    // Restore current_task_ptr_ from index
    set_current_index(current_idx_in);

    // Ready-queue state is restored separately via restore_pod()
    (void)rq_bitmap_hi;
    (void)rq_bitmap_lo;
    ready_queue_.reset();

    {
        uint32_t _a, _b, _c, _d;
        asm volatile("cpuid" : "=a"(_a), "=b"(_b), "=c"(_c), "=d"(_d) : "a"(0));
    }
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_next_task_id, UINT64_MAX, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&isr_nesting_depth, (uint64_t)0, __ATOMIC_RELEASE);
}

void Scheduler::clear_switch_globals() noexcept {
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr,
                     __ATOMIC_RELEASE);
}

void Scheduler::rebuild_all_tasks() noexcept {
    all_tasks_.rebuild();
}

void Scheduler::capture_task_fields(TaskFields *out) {
    uint64_t idx = 0;
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (t->magic != TaskControlBlock::TCB_MAGIC) {
            if (idx < MAX_TASKS)
                out[idx].magic = 0;
            ++idx;
            continue;
        }
        if (idx >= MAX_TASKS)
            break;
        out[idx].magic = t->magic;
        out[idx].id = t->id;
        out[idx].parent_id = t->parent_id;
        out[idx].state = t->state;
        out[idx].priority = t->priority;
        out[idx].base_priority = t->base_priority;
        out[idx].period_ticks = t->period_ticks;
        out[idx].deadline_ticks = t->deadline_ticks;
        out[idx].deadline_missed = t->deadline_missed;
        out[idx].deadline_miss_count = t->deadline_miss_count;
        out[idx].executed_ticks = t->executed_ticks;
        out[idx].remaining_ticks = t->remaining_ticks;
        out[idx].exit_code = t->exit_code;
        out[idx].context = t->context;
        out[idx].kernel_stack_top = t->kernel_stack_top;
        out[idx].waiting_child_pid = t->waiting_child_pid;
        out[idx].waiting_child_status =
            reinterpret_cast<uint64_t>(t->waiting_child_status);
        out[idx].pending_signals = t->pending_signals;
        out[idx].alarm_ticks = t->alarm_ticks;
        out[idx].alarm_armed = t->alarm_armed;
        out[idx].runq_next = t->runq_next_;
        out[idx].runq_prev = t->runq_prev_;
        out[idx].in_ready_queue = t->in_ready_queue_;
        out[idx].rq_priority = t->rq_priority_;
        ++idx;
    }
    while (idx < MAX_TASKS)
        out[idx++].magic = 0;
}

void Scheduler::restore_task_fields(const TaskFields *saved) {
    for (auto *t = all_tasks_.first_ptr(); t; t = all_tasks_.next_ptr(t)) {
        if (t->magic != TaskControlBlock::TCB_MAGIC)
            continue;
        for (uint64_t j = 0; j < MAX_TASKS; ++j) {
            if (saved[j].magic != TaskControlBlock::TCB_MAGIC)
                continue;
            if (saved[j].id != t->id)
                continue;
            t->state = saved[j].state;
            t->priority = saved[j].priority;
            t->base_priority = saved[j].base_priority;
            t->period_ticks = saved[j].period_ticks;
            t->deadline_ticks = saved[j].deadline_ticks;
            t->deadline_missed = saved[j].deadline_missed;
            t->deadline_miss_count = saved[j].deadline_miss_count;
            t->executed_ticks = saved[j].executed_ticks;
            t->remaining_ticks = saved[j].remaining_ticks;
            t->exit_code = saved[j].exit_code;
            t->context = saved[j].context;
            t->kernel_stack_top = saved[j].kernel_stack_top;
            t->pending_signals = saved[j].pending_signals;
            t->alarm_ticks = saved[j].alarm_ticks;
            t->alarm_armed = saved[j].alarm_armed;
            t->runq_next_ = saved[j].runq_next;
            t->runq_prev_ = saved[j].runq_prev;
            t->in_ready_queue_ = saved[j].in_ready_queue;
            t->rq_priority_ = saved[j].rq_priority;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// P5a: Deferred-kill helpers
// ---------------------------------------------------------------------------

void Scheduler::defer_kill(TaskControlBlock *task) noexcept {
    if (s_deferred_kill_count < MAX_DEFERRED_KILLS) {
        s_deferred_kill_tasks[s_deferred_kill_count++] = task;
    } else {
        Logger::warn("[DMD] deferred-kill list full, task %lu not added",
                     task->id);
    }
}

void Scheduler::process_deferred_kills() noexcept {
    for (uint64_t i = 0; i < s_deferred_kill_count; ++i) {
        auto *task = s_deferred_kill_tasks[i];
        if (!task || task->magic != TaskControlBlock::TCB_MAGIC)
            continue;

        if (task->sporadic_server) {
            task->sporadic_server->on_completion(arch::Timer::ticks());
        }

        Logger::info("[DMD] Task %lu (%s) killed and cleaned up", task->id,
                     task->name);
        task->cleanup();
        Scheduler::remove_task(*task);
        MemPool::free(task);
    }
    s_deferred_kill_count = 0;
}

// ---------------------------------------------------------------------------
// P6b: Deadline scan (monitor path)
// ---------------------------------------------------------------------------

#if CONFIG_DEADLINE_MONITOR_TASK
void Scheduler::scan_deadlines() noexcept {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
#if CONFIG_DEADLINE_MISS_DETECTION
    // Scan every live task rather than only deadline_list_.  A task's deadline
    // may be (re)configured after add_task() returned, in which case it is not
    // a member of deadline_list_ yet — walking only that list would miss its
    // deadline and silently suppress the miss detection.  Scanning all tasks
    // is O(n) but n is small and keeps detection correct for every periodic
    // task regardless of when its deadline was set.
    const uint64_t now = arch::Timer::ticks();
    for (auto *task = all_tasks_.first_ptr(); task;
         task = all_tasks_.next_ptr(task)) {
        if (task->magic != TaskControlBlock::TCB_MAGIC)
            continue;
        if (task->state == TaskState::TERMINATED)
            continue;
        if (task->period_ticks == 0 || task->deadline_ticks == 0)
            continue;
        if (task->deadline_missed)
            continue;
        if (task->deadline_ticks >= now)
            continue;
        if (task->sporadic_server) {
            task->ss_state_on_deadline_miss =
                static_cast<uint8_t>(task->sporadic_server->state());
            task->ss_budget_on_deadline_miss =
                task->sporadic_server->remaining_budget();
        }
        task->deadline_missed = true;
        ++task->deadline_miss_count;
        deadline_miss_handler(task, now - task->deadline_ticks);
        task->deadline_ticks += task->period_ticks;
        task->deadline_missed = false;
#if CONFIG_WCET_OVERRUN_DETECTION
        task->wcet_overrun_fired = false;
#endif
    }
#endif
    __atomic_fetch_add(&deadline_detection_integrity, 1, __ATOMIC_RELEASE);
}

void Scheduler::monitor_task_entry() noexcept {
    auto *me = Scheduler::current_task();
    while (true) {
        arch::pause();
        {
            arch::IrqGuard guard{};
            scheduler_lock_.lock();
            dequeue_ready(*me);
            scheduler_lock_.unlock();
            me->state = TaskState::BLOCKED;
        }
        Scheduler::reschedule();

        if (__atomic_exchange_n(&s_scan_requested_, 0, __ATOMIC_ACQUIRE)) {
            scan_deadlines();
        }
    }
}

void Scheduler::ensure_monitor() noexcept {
    bool need_spawn = true;
    if (s_monitor_task_) {
        if (s_monitor_task_->magic == TaskControlBlock::TCB_MAGIC &&
            s_monitor_task_->state != TaskState::TERMINATED) {
            need_spawn = false;
        }
    }
    if (!need_spawn)
        return;

    Logger::info("[MON] Re-spawning deadline-monitor task");
    auto *tcb = TaskControlBlock::create(monitor_task_entry, 127, 0xFFFFFFFF);
    if (tcb) {
        s_monitor_task_ = tcb;
        s_scan_requested_ = false;
        add_task(*tcb);
        SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
        tcb->state = TaskState::BLOCKED;
        dequeue_ready(*tcb);
    }
}
#endif // CONFIG_DEADLINE_MONITOR_TASK

// ---------------------------------------------------------------------------
// Deadline-miss / WCET handlers (weak defaults)
// ---------------------------------------------------------------------------

#if CONFIG_DEADLINE_MISS_DETECTION
__attribute__((weak)) void
deadline_miss_handler(TaskControlBlock *task,
                      uint64_t missed_by_ticks) noexcept {
    bool budget_exhausted = (task->sporadic_server != nullptr &&
                             static_cast<task::SporadicServer::State>(
                                 task->ss_state_on_deadline_miss) ==
                                 task::SporadicServer::State::EXHAUSTED);

#if CONFIG_DEADLINE_ACTION == 1
    if (budget_exhausted)
        Logger::error("[DMD] Task %lu (%s) budget exhausted (state=EXHAUSTED, "
                      "action=PANIC)",
                      task->id, task->name);
    else
        Logger::error("[DMD] Task %lu (%s) missed deadline by %lu ticks "
                      "(action=PANIC)",
                      task->id, task->name, missed_by_ticks);
    panic("[DMD] deadline miss (action=PANIC)");
#elif CONFIG_DEADLINE_ACTION == 2
    if (budget_exhausted)
        Logger::warn("[DMD] Task %lu (%s) budget exhausted (state=EXHAUSTED, "
                     "action=DEMOTE)",
                     task->id, task->name);
    else
        Logger::warn(
            "[DMD] Task %lu (%s) missed deadline by %lu ticks (action=DEMOTE)",
            task->id, task->name, missed_by_ticks);
    if (task->priority > 1)
        task->priority >>= 1;
#elif CONFIG_DEADLINE_ACTION == 3
    if (budget_exhausted)
        Logger::warn("[DMD] Task %lu (%s) budget exhausted (state=EXHAUSTED, "
                     "action=KILL)",
                     task->id, task->name);
    else
        Logger::warn(
            "[DMD] Task %lu (%s) missed deadline by %lu ticks (action=KILL)",
            task->id, task->name, missed_by_ticks);
    task->state = TaskState::TERMINATED;
    task->exit_code =
        static_cast<uint64_t>(-static_cast<int64_t>(Signal::SIGKILL));
    wake_waiting_parent(*task);
    Scheduler::defer_kill(task);
#elif CONFIG_DEADLINE_ACTION == 4
    if (budget_exhausted)
        Logger::info("[DMD] Task %lu (%s) budget exhausted (state=EXHAUSTED, "
                      "action=NOTIFY_MONITOR)",
                      task->id, task->name);
    else
        Logger::info("[DMD] Task %lu (%s) missed deadline by %lu ticks "
                      "(action=NOTIFY_MONITOR)",
                      task->id, task->name, missed_by_ticks);
    // Resolve the monitor PID: compile-time CONFIG_DEADLINE_MONITOR_PID when
    // set, otherwise the test-only override so the config matrix can target
    // the live [deadline-mon] task without a fixed compile-time PID.
    uint64_t monitor_pid = (CONFIG_DEADLINE_MONITOR_PID > 0)
                               ? static_cast<uint64_t>(CONFIG_DEADLINE_MONITOR_PID)
                               : g_test_deadline_monitor_pid;
    auto *monitor = Scheduler::find_task(monitor_pid);
    if (monitor && monitor->magic == TaskControlBlock::TCB_MAGIC &&
        monitor->state != TaskState::TERMINATED) {
        monitor->pending_signals |=
            (1ULL << static_cast<uint64_t>(Signal::SIGUSR1));
    }
#else
    if (budget_exhausted)
        Logger::info("[DMD] Task %lu (%s) budget exhausted (budget=%lu, "
                     "state=EXHAUSTED, action=LOG_ONLY)",
                     task->id, task->name, task->ss_budget_on_deadline_miss);
    else
        Logger::info(
            "[DMD] Task %lu (%s) missed deadline by %lu ticks (action=LOG_ONLY)",
            task->id, task->name, missed_by_ticks);
#endif
}
#endif

#if CONFIG_WCET_OVERRUN_DETECTION
__attribute__((weak)) void
wcet_overrun_handler(TaskControlBlock *task,
                     uint64_t overrun_by_ticks) noexcept {
    Logger::info(
        "[WCET] Task %lu (%s) exceeded WCET by %lu ticks (action=LOG_ONLY)",
        task->id, task->name, overrun_by_ticks);
}
#endif

} // namespace kernel

extern "C" void scheduler_diag_pre_save() {
#ifdef CONFIG_DEBUG
    uint64_t rsp{};
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    auto *cur = kernel::Scheduler::current_task();
    auto cidx = kernel::Scheduler::current_index();
    if (cur && cur->magic == kernel::TaskControlBlock::TCB_MAGIC) {
        auto base = reinterpret_cast<uint64_t>(cur->kernel_stack);
        auto top = cur->kernel_stack_top;
        if (rsp < base || rsp > top) {
            kernel::Logger::raw_write("[DIAG] pre-save: idx=");
            kernel::Logger::print_dec(cidx);
            kernel::Logger::raw_write(" id=");
            kernel::Logger::print_dec(cur->id);
            kernel::Logger::raw_write(" cur_rsp=0x");
            kernel::Logger::print_hex(rsp);
            kernel::Logger::raw_write(" ctx_rsp=0x");
            kernel::Logger::print_hex(cur->context.rsp);
            kernel::Logger::raw_write(" state=");
            kernel::Logger::print_dec(static_cast<uint64_t>(cur->state));
            kernel::Logger::raw_write(" kstack=[0x");
            kernel::Logger::print_hex(base);
            kernel::Logger::raw_write("-0x");
            kernel::Logger::print_hex(top);
            kernel::Logger::raw_write("]");
            // Scan all tasks to find the real owner of the live RSP.
            kernel::Logger::raw_write(" owners: ");
            for (uint64_t ti = 0; ti < kernel::Scheduler::task_count(); ++ti) {
                auto *tt = kernel::Scheduler::task_at(ti);
                if (!tt || tt->magic != kernel::TaskControlBlock::TCB_MAGIC)
                    continue;
                uint64_t tb =
                    reinterpret_cast<uint64_t>(tt->kernel_stack);
                if (rsp >= tb && rsp < tt->kernel_stack_top) {
                    kernel::Logger::raw_write("T");
                    kernel::Logger::print_dec(tt->id);
                    kernel::Logger::raw_write("(0x");
                    kernel::Logger::print_hex(tb);
                    kernel::Logger::raw_write("-0x");
                    kernel::Logger::print_hex(tt->kernel_stack_top);
                    kernel::Logger::raw_write(") ");
                }
            }
            kernel::Logger::raw_write("\n");
        }
    } else if (!cur) {
        kernel::Logger::raw_write("[DIAG] pre-save: idx=");
        kernel::Logger::print_dec(cidx);
        kernel::Logger::raw_write(" cur=NULL\n");
    } else {
        kernel::Logger::raw_write("[DIAG] pre-save: idx=");
        kernel::Logger::print_dec(cidx);
        kernel::Logger::raw_write(" id=");
        kernel::Logger::print_dec(cur->id);
        kernel::Logger::raw_write(" magic=0x");
        kernel::Logger::print_hex(cur->magic);
        kernel::Logger::raw_write(" bad_magic\n");
    }
#else
    (void)0;
#endif
}

namespace kernel {
using namespace errors;

SchedulerError Scheduler::init_err() {
    init();
    return SCHED_ERR_OK;
}

SchedulerError Scheduler::add_task_err(TaskControlBlock &task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    if (all_tasks_.size() >= MAX_TASKS)
        return SCHED_ERR_TABLE_FULL;
    if (id_table_find(task.id) != nullptr)
        return SCHED_ERR_DUPLICATE_ID;
    all_tasks_.append(&task);
    if (task.period_ticks > 0 && task.deadline_ticks > 0) {
        deadline_list_.insert(&task);
    }
    id_table_insert(task.id, &task);
    ready_queue_.enqueue(task, effective_priority(&task));
    kernel::test::ResourceTracker::instance().track_task_add();
    return SCHED_ERR_OK;
}

SchedulerError Scheduler::remove_task_err(TaskControlBlock &task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    if (id_table_find(task.id) == nullptr)
        return SCHED_ERR_NOT_FOUND;
    all_tasks_.remove(&task);
    deadline_list_.remove(&task);
    id_table_remove(&task);
    dequeue_ready(task);

    __atomic_store_n(&scheduler_save_rsp_to, nullptr, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);

    kernel::test::ResourceTracker::instance().track_task_remove();
    return SCHED_ERR_OK;
}

SchedulerError Scheduler::alloc_id_err(uint64_t &out_id) {
    out_id = next_task_id_++;
    return SCHED_ERR_OK;
}

} // namespace kernel

extern "C" void scheduler_on_context_switch() {
    uint64_t id =
        __atomic_load_n(&kernel::scheduler_next_task_id, __ATOMIC_ACQUIRE);
    if (id == UINT64_MAX)
        return;
    __atomic_store_n(&kernel::scheduler_next_task_id, UINT64_MAX,
                     __ATOMIC_RELEASE);
    kernel::Scheduler::set_current_by_id(id);
}
