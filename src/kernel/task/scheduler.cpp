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

#include <kernel/task/scheduler.hpp>
#include <kernel/arch/gdt.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/sync/spinlock_guard.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/memory/integrity.hpp>
#include <kernel/test/resource_tracker.hpp>
#include <kernel/test/test_watchdog.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/driver/iocd.hpp>

// Architecture-aware stack pointer access
#if defined(CONFIG_ARCH_X86_64)
#  define TASK_STACK_PTR(t) ((t)->context.rsp)
#elif defined(CONFIG_ARCH_AARCH64)
#  define TASK_STACK_PTR(t) ((t)->context.sp_el0)
#elif defined(CONFIG_ARCH_RISCV64)
#  define TASK_STACK_PTR(t) ((t)->context.sp)
#endif
#include <kernel/task/sporadic_server.hpp>
#include <signal.hpp>
#include <logger.hpp>
#include <assert.hpp>

namespace kernel {

/// @brief Returns the effective scheduling priority for a task.
/// If the task has a SporadicServer, the server's current priority
/// (which drops to bg_priority_ when budget is exhausted) is used.
/// Otherwise the raw TCB priority applies.
static inline uint64_t effective_priority(const TaskControlBlock* t) noexcept {
    if (t && t->sporadic_server)
        return t->sporadic_server->current_priority();
    return t ? t->priority : 0;
}

void Scheduler::enqueue_ready(TaskControlBlock& task) noexcept {
    ready_queue_.enqueue(task, effective_priority(&task));
}

void Scheduler::dequeue_ready(TaskControlBlock& task) noexcept {
    ready_queue_.remove(task, effective_priority(&task));
}

void Scheduler::set_task_ready(TaskControlBlock& task) noexcept {
    task.state = TaskState::READY;
    enqueue_ready(task);
}

TaskControlBlock* const Scheduler::ID_TOMBSTONE =
    reinterpret_cast<TaskControlBlock*>(static_cast<uintptr_t>(1));

TaskControlBlock* Scheduler::tasks_[MAX_TASKS] = {};
TaskControlBlock* Scheduler::id_table_[ID_TABLE_SIZE] = {};
uint64_t Scheduler::task_count_ = 0;
uint64_t Scheduler::current_index_ = 0;
uint64_t Scheduler::next_task_id_ = 1;
uint64_t Scheduler::sporadic_task_count_ = 0;
bool Scheduler::preempt_enabled_ = false;
ReadyQueueManager Scheduler::ready_queue_;
TaskControlBlock* Scheduler::idle_task_ = nullptr;
TaskControlBlock* Scheduler::shell_task_ptr_ = nullptr;
sync::SpinLock Scheduler::scheduler_lock_;

// Liu-Leyland Rate-Monotonic LUB bounds (scaled by 1000000)
static constexpr uint64_t LIU_LEYLAND_MAX_TASKS = 20;
static constexpr uint32_t LIU_LEYLAND_BOUNDS[LIU_LEYLAND_MAX_TASKS + 1] = {
    0,         // n = 0
    1000000,   // n = 1 (100.0%)
    828427,   // n = 2 (82.8%)
    779763,   // n = 3 (78.0%)
    756828,   // n = 4 (75.7%)
    743491,   // n = 5 (74.3%)
    734772,   // n = 6 (73.5%)
    728626,   // n = 7 (72.8%)
    724061,   // n = 8 (72.4%)
    720537,   // n = 9 (72.0%)
    717734,   // n = 10 (71.7%)
    715451,   // n = 11 (71.5%)
    713557,   // n = 12 (71.3%)
    711958,   // n = 13 (71.2%)
    710592,   // n = 14 (71.0%)
    709409,   // n = 15 (70.9%)
    708378,   // n = 16 (70.8%)
    707472,   // n = 17 (70.7%)
    706669,   // n = 18 (70.6%)
    705952,   // n = 19 (70.6%)
    705298    // n = 20 (70.5%)
};
static constexpr uint32_t LIU_LEYLAND_LIMIT = 693147;

void Scheduler::init() {
    for (uint64_t i = 0; i < ID_TABLE_SIZE; ++i) {
        id_table_[i] = nullptr;
    }

    idle_task_ = TaskControlBlock::create(kernel::integrity::idle_task_main, 0, 0xFFFFFFFF);
    idle_task_->state = TaskState::READY;

    tasks_[0] = idle_task_;
    id_table_insert(idle_task_->id, idle_task_);
    task_count_ = 1;
    current_index_ = 0;
    sporadic_task_count_ = 0;
    preempt_enabled_ = true;
}

void Scheduler::add_task(TaskControlBlock& task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    ENSURE(task_count_ < MAX_TASKS);
    tasks_[task_count_++] = &task;
    id_table_insert(task.id, &task);
    task.in_ready_queue_ = false;
    task.runq_next_ = nullptr;
    task.runq_prev_ = nullptr;
    ready_queue_.enqueue(task, effective_priority(&task));
    kernel::test::ResourceTracker::instance().track_task_add();

    // Liu-Leyland LUB admission test: warn if total utilization exceeds
    // the rate-monotonic schedulability bound
    if (task.period_ticks > 0 && task.period_ticks <= 100) {
        // Use wcet = period_ticks (100% CPU-bound worst-case) for the check
        uint64_t total_util = 0;
        for (uint64_t i = 0; i < task_count_; ++i) {
            auto* t = tasks_[i];
            if (t->magic != TaskControlBlock::TCB_MAGIC) continue;
            if (t->period_ticks > 0) {
                uint64_t util = ((uint64_t)t->period_ticks * 1000000) / t->period_ticks;
                total_util += util;
            }
        }
        uint32_t bound = 0;
        if (task_count_ <= LIU_LEYLAND_MAX_TASKS) {
            bound = LIU_LEYLAND_BOUNDS[task_count_];
        } else {
            bound = LIU_LEYLAND_LIMIT;
        }
        if (total_util > bound) {
            Logger::warn("Scheduler: task %d (prio=%d, period=%d) exceeds "
                         "Liu-Leyland bound (%d > %d) — overrun possible",
                         task.id, task.priority, task.period_ticks,
                         total_util, bound);
        }
    }

}

void Scheduler::remove_task(TaskControlBlock& task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    id_table_remove(&task);
    dequeue_ready(task);
    kernel::test::ResourceTracker::instance().track_task_remove();
    for (uint64_t i = 0; i < task_count_; ++i) {
        if (tasks_[i] == &task) {
            tasks_[i] = tasks_[--task_count_];
            if (current_index_ == task_count_) {
                // Current task was at the last slot — now moved to slot i
                current_index_ = (i < task_count_) ? i : 0;
            } else if (current_index_ >= task_count_) {
                // Out-of-bounds (should not happen) — reset to 0
                current_index_ = 0;
            }
            break;
        }
    }
}

TaskControlBlock* Scheduler::current_task() noexcept {
    if (task_count_ == 0) return nullptr;
    return tasks_[current_index_];
}

uint64_t Scheduler::task_count() noexcept {
    return task_count_;
}

TaskControlBlock* Scheduler::task_at(uint64_t index) noexcept {
    if (index >= task_count_) return nullptr;
    return tasks_[index];
}

TaskControlBlock* Scheduler::find_task(uint64_t id) noexcept {
    return id_table_find(id);
}

bool Scheduler::needs_switch() noexcept {
    if (task_count_ <= 1) return false;
    auto* current = tasks_[current_index_];
    if (!current || current->magic != TaskControlBlock::TCB_MAGIC) return false;
    if (current == idle_task_) return true;

    uint64_t cur_eff = effective_priority(current);
    for (uint64_t i = 1; i < task_count_; ++i) {
        auto* task = tasks_[i];
        if (task == current) continue;
        if (task->magic != TaskControlBlock::TCB_MAGIC) continue;
        if (task->state == TaskState::READY || task->state == TaskState::RUNNING
            ) {
            if (effective_priority(task) > cur_eff) return true;
        }
    }
    return false;
}

TaskControlBlock* Scheduler::next_task() noexcept {
    if (task_count_ <= 1) return tasks_[0];

    // O(1) fast path: dequeue from ready queue
    {
        auto* next = ready_queue_.dequeue_highest();
        if (next) return next;
    }

    // Lazy rebuild: clear and rebuild ready queue from tasks_[]
    // Handles edge cases where tasks became READY outside the scheduler
    // (test code, race conditions) without going through set_task_ready().
    ready_queue_.clear_all();
    for (uint64_t i = 0; i < task_count_; ++i) {
        auto* task = tasks_[i];
        if (task->magic != TaskControlBlock::TCB_MAGIC) continue;
        if (task->state == TaskState::READY) {
            ready_queue_.enqueue(*task, effective_priority(task));
        }
    }

    {
        auto* next = ready_queue_.dequeue_highest();
        if (next) return next;
    }

    // Nothing ready — return current or idle
    auto* cur = current_task();
    return cur ? cur : tasks_[0];
}

void Scheduler::set_current(TaskControlBlock& task) noexcept {
    for (uint64_t i = 0; i < task_count_; ++i) {
        if (tasks_[i] == &task) {
            current_index_ = i;
            return;
        }
    }
}

void Scheduler::set_current_by_id(uint64_t id) noexcept {
    for (uint64_t i = 0; i < task_count_; ++i) {
        auto* t = tasks_[i];
        if (t && t->magic == TaskControlBlock::TCB_MAGIC && t->id == id) {
            current_index_ = i;
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

void Scheduler::id_table_insert(uint64_t id, TaskControlBlock* tcb) {
    uint64_t idx = id_table_probe(id);
    while (id_table_[idx] != nullptr && id_table_[idx] != ID_TOMBSTONE) {
        idx = (idx + 1) & ID_TABLE_MASK;
    }
    id_table_[idx] = tcb;
}

uint64_t Scheduler::alloc_id() noexcept {
    return next_task_id_++;
}

void Scheduler::id_table_remove(TaskControlBlock* task) {
    uint64_t idx = id_table_probe(task->id);
    while (id_table_[idx] != nullptr) {
        if (id_table_[idx] != ID_TOMBSTONE && id_table_[idx] == task) {
            id_table_[idx] = ID_TOMBSTONE;
            return;
        }
        idx = (idx + 1) & ID_TABLE_MASK;
    }
}

TaskControlBlock* Scheduler::id_table_find(uint64_t id) {
    uint64_t idx = id_table_probe(id);
    while (id_table_[idx] != nullptr) {
        if (id_table_[idx] != ID_TOMBSTONE && id_table_[idx]->id == id) {
            return id_table_[idx];
        }
        idx = (idx + 1) & ID_TABLE_MASK;
    }
    return nullptr;
}

void Scheduler::on_tick() noexcept {
    uint64_t current_tick = arch::Timer::ns();

    if (!preempt_enabled_) return;



    for (uint64_t i = 0; i < task_count_; ++i) {
        auto* task = tasks_[i];
        if (task->magic != TaskControlBlock::TCB_MAGIC) continue;
        if (task->state == TaskState::RUNNING || task->state == TaskState::READY
            ) {
            ++task->executed_ticks;
            if (task->remaining_ticks > 0) --task->remaining_ticks;

            // Check for alarm expiration
            if (task->alarm_armed && current_tick >= task->alarm_ticks) {
                task->alarm_armed = false;
                // Deliver SIGALRM to the task
                task->pending_signals |= (1ULL << static_cast<uint64_t>(Signal::
                    SIGALRM));
            }
        }
    }

    // Sporadic Server budget management: process replenishments for all
    // sporadic-server tasks, bounded by CONFIG_SPORADIC_SERVER_MAX_TASKS,
    // and consume one tick for the current task.
    {
        auto* cur = current_task();
        uint64_t found = 0;
        for (uint64_t i = 0; i < task_count_ && found < sporadic_task_count_; ++i) {
            auto* t = tasks_[i];
            if (t->magic != TaskControlBlock::TCB_MAGIC) continue;
            if (t->sporadic_server) {
                found++;
                if (reinterpret_cast<uint64_t>(t->sporadic_server) == 0xFFFFFFFFFFFFFFFFULL) {
                    Logger::raw_write("[BUG] on_tick: t=");
                    Logger::print_dec(t->id);
                    Logger::raw_write(" sporadic_server=-1 at i=");
                    Logger::print_dec(i);
                    Logger::raw_write("\n");
                    continue;
                }
                t->sporadic_server->process_replenishments(current_tick);
                if (t == cur && t->sporadic_server->is_active()) {
                    if (!t->sporadic_server->consume(current_tick)) {
                        Scheduler::reschedule();
                    }
                }
            }
        }
    }

    // Context switches are deferred to the outermost ISR epilogue, so
    // it is safe to call rate_monotonic_schedule even from a nested ISR.
    // The globals (scheduler_save_rsp_to, etc.) survive until consumed.

    static uint64_t tick_counter = 0;
    ++tick_counter;
    if (tick_counter % 100 == 0) {
        reap_orphans();
        daemon::restart_stale_daemons();
    }

    rate_monotonic_schedule();
}

void Scheduler::reap_orphans() noexcept {
    static bool inside_reap = false;
    if (inside_reap) return;
    inside_reap = true;

    auto* current = current_task();
    auto* init_task = (task_count_ > 0) ? tasks_[0] : nullptr;
    TaskControlBlock* new_idle = nullptr;

    // Single pass: null-mark reaped tasks, compact at the end.
    for (uint64_t i = 0; i < task_count_; ++i) {
        auto* t = tasks_[i];
        if (!t) continue;
        if (t->magic != TaskControlBlock::TCB_MAGIC) continue;
        if (t->state != TaskState::TERMINATED) continue;
        if (t != idle_task_ && t == current) continue;

        // Adopt children to init_task so they become orphans.
        if (init_task && init_task != t) {
            if (t->first_child) {
                auto* child = t->first_child;
                t->first_child = nullptr;
                if (t->num_children > 0) t->num_children = 0;
                while (child) {
                    auto* next = child->next_sibling;
                    child->prev_sibling = nullptr;
                    child->next_sibling = nullptr;
                    child->parent_id = 0;
                    init_task->add_child(child);
                    child = next;
                }
            }
            for (TaskIter it(0);;) {
                auto* c = it.next(t);
                if (!c) break;
                if (c == init_task) continue;
                if (c->parent_id == t->id) {
                    c->parent_id = 0;
                    if (t->num_children > 0) --t->num_children;
                    init_task->add_child(c);
                }
            }
        }

        // Determine if this task can be reaped.
        bool can_reap = false;
        if (t->parent_id == 0) {
            can_reap = true;
        } else if (init_task && t->parent_id == init_task->id &&
                   init_task->state == TaskState::RUNNING) {
            // Parent is init and alive — init never blocks on waitpid
            // in its main loop; children of init are always reapable.
            can_reap = true;
        } else {
            bool parent_found = false;
            for (TaskIter it(0);;) {
                auto* p = it.next();
                if (!p) break;
                if (p->id == t->parent_id) {
                    parent_found = true;
                    if (p->state == TaskState::TERMINATED) {
                        can_reap = true;
                    } else if (p->waiting_child_pid != 0 &&
                               p->waiting_child_pid != t->id &&
                               p->waiting_child_pid != static_cast<uint64_t>(-1)) {
                        can_reap = true;
                    } else if (p->waiting_child_pid == 0) {
                        can_reap = true;
                    }
                    break;
                }
            }
            if (!parent_found) can_reap = true;
        }

        if (!can_reap) continue;

        // If this task shares its page table, defer until no children depend on it.
        if (!t->page_table_shared_) {
            bool has_sharing_child = false;
            for (TaskIter it(0);;) {
                auto* c = it.next(t);
                if (!c) break;
                if (c == current) continue;
                if (c->parent_id == t->id && c->page_table_shared_) {
                    has_sharing_child = true;
                    break;
                }
            }
            if (has_sharing_child) continue;
        }

        // Destroy the task (no compaction during scan).
        if (t == idle_task_) {
            auto* created = TaskControlBlock::create(
                kernel::integrity::idle_task_main, 0, 0xFFFFFFFF);
            created->state = TaskState::READY;
            dequeue_ready(*t);
            t->cleanup();
            id_table_remove(t);
            MemPool::free(t);
            tasks_[i] = nullptr;
            new_idle = created;
        } else {
            t->cleanup();
            id_table_remove(t);
            dequeue_ready(*t);
            MemPool::free(t);
            tasks_[i] = nullptr;
        }
    }

    // Compact the task array, removing null slots and dangling pointers.
    uint64_t wr = 0;
    for (uint64_t rd = 0; rd < task_count_; ++rd) {
        if (tasks_[rd]) {
            if (tasks_[rd]->magic != TaskControlBlock::TCB_MAGIC) {
                tasks_[rd] = nullptr;
                continue;
            }
            tasks_[wr++] = tasks_[rd];
        }
    }

    // If idle was recreated, insert it at index 0.
    if (new_idle) {
        for (uint64_t k = wr; k > 0; --k)
            tasks_[k] = tasks_[k - 1];
        tasks_[0] = new_idle;
        ++wr;
        idle_task_ = new_idle;
        id_table_insert(new_idle->id, new_idle);
    }

    task_count_ = wr;

    // Restore current_index_ for the calling task.
    current_index_ = 0;
    TaskControlBlock* target = (current == idle_task_ && new_idle) ? new_idle : current;
    for (uint64_t i = 0; i < task_count_; ++i) {
        if (tasks_[i] == target) {
            current_index_ = i;
            break;
        }
    }

    inside_reap = false;
}

void Scheduler::cleanup_test_tasks() noexcept {
    for (uint64_t i = 1; i < task_count_; ++i) {
        auto* t = tasks_[i];
        if (t && t->magic == TaskControlBlock::TCB_MAGIC &&
            t->state != TaskState::TERMINATED) {
            t->state = TaskState::TERMINATED;
            t->exit_code = 0;
        }
    }
    reap_orphans();
    uint64_t i = 1;
    while (i < task_count_) {
        auto* t = tasks_[i];
        if (t && t != idle_task_ &&
            t->magic == TaskControlBlock::TCB_MAGIC) {
            t->cleanup();
            remove_task(*t);
            MemPool::free(t);
        } else {
            ++i;
        }
    }
    for (uint64_t i = 0; i < ID_TABLE_SIZE; ++i) id_table_[i] = nullptr;
    id_table_insert(idle_task_->id, idle_task_);
    task_count_ = 1;
    tasks_[0] = idle_task_;
    current_index_ = 0;
    // Drain ready queue — will be repopulated as tasks become READY
    ready_queue_.reset();
}

static void report_corruption(const char* label) {
    (void)label;
    __atomic_fetch_add(&scheduler_corruption_count, 1, __ATOMIC_RELEASE);
#ifdef CONFIG_DEBUG
    ENSURE(false && "scheduler corruption detected — see log above");
#endif
}

static bool rsp_in_stack_range(uint64_t rsp, const TaskControlBlock* t, const char* label) {
    auto base = reinterpret_cast<uint64_t>(t->kernel_stack);
    auto top = t->kernel_stack_top;
    if (rsp >= base && rsp <= top) return true;
    Logger::raw_write("[SCHED] "); Logger::raw_write(label);
    Logger::raw_write(": task id="); Logger::print_dec(t->id);
    Logger::raw_write(" rsp=0x"); Logger::print_hex(rsp);
    Logger::raw_write(" outside stack [0x"); Logger::print_hex(base);
    Logger::raw_write("-0x"); Logger::print_hex(top);
    Logger::raw_write("]\n");
    return false;
}

static bool validate_switch(TaskControlBlock* current, TaskControlBlock* next, const char* label) {
    if (!current) { Logger::raw_write("[SCHED] "); Logger::raw_write(label); Logger::raw_write(": current=null\n"); return false; }
    if (!next)   { Logger::raw_write("[SCHED] "); Logger::raw_write(label); Logger::raw_write(": next=null\n"); return false; }
    auto caddr = reinterpret_cast<uint64_t>(current);
    auto naddr = reinterpret_cast<uint64_t>(next);
    if (caddr < 0xFFFF800000000000ULL) { Logger::raw_write("[SCHED] "); Logger::raw_write(label); Logger::raw_write(": current low 0x"); Logger::print_hex(caddr); Logger::raw_write("\n"); return false; }
    if (naddr < 0xFFFF800000000000ULL) { Logger::raw_write("[SCHED] "); Logger::raw_write(label); Logger::raw_write(": next low 0x"); Logger::print_hex(naddr); Logger::raw_write("\n"); return false; }
    if (current->magic != TaskControlBlock::TCB_MAGIC) { Logger::raw_write("[SCHED] "); Logger::raw_write(label); Logger::raw_write(": current magic=0x"); Logger::print_hex(current->magic); Logger::raw_write("\n"); return false; }
    if (next->magic != TaskControlBlock::TCB_MAGIC) { Logger::raw_write("[SCHED] "); Logger::raw_write(label); Logger::raw_write(": next magic=0x"); Logger::print_hex(next->magic); Logger::raw_write("\n"); return false; }
    if (!rsp_in_stack_range(TASK_STACK_PTR(next), next, label)) {
        Logger::raw_write("[SCHED] "); Logger::raw_write(label);
        Logger::raw_write(": current id="); Logger::print_dec(current->id);
        Logger::raw_write(" rsp=0x"); Logger::print_hex(TASK_STACK_PTR(current));
        Logger::raw_write(" state="); Logger::print_dec(static_cast<uint64_t>(current->state));
        Logger::raw_write(" kstack=[0x"); Logger::print_hex(reinterpret_cast<uint64_t>(current->kernel_stack));
        Logger::raw_write("-0x"); Logger::print_hex(current->kernel_stack_top);
        Logger::raw_write("]\n");
        return false;
    }
    return true;
}

void Scheduler::cleanup_zombies() noexcept {
    auto* shell = shell_task_ptr_;
    if (!shell ||
        (reinterpret_cast<uint64_t>(shell) & 0xFFFF800000000000ULL) != 0xFFFF800000000000ULL ||
        shell->magic != TaskControlBlock::TCB_MAGIC) {
        shell = current_task();
    }
    uint64_t vfsd_pid = vfsd::get_vfsd_pid();
    uint64_t iocd_pid = iocd::get_iocd_pid();

    uint64_t max_rd = task_count_;
    if (max_rd > MAX_TASKS) max_rd = MAX_TASKS;
    uint64_t wr = 0;
    for (uint64_t rd = 0; rd < max_rd; ++rd) {
        auto* t = tasks_[rd];
        bool keep = true;

        if (!t) {
            keep = false;
        } else if (t->magic != TaskControlBlock::TCB_MAGIC) {
            keep = false;
        } else if (t == idle_task_ || t == shell ||
                   t->id == vfsd_pid || t->id == iocd_pid) {
            keep = true;
        } else {
            // Remove from ready queue before cleanup
            if (t->state == TaskState::READY) {
                Scheduler::dequeue_ready(*t);
            }
            t->cleanup();
            id_table_remove(t);
            MemPool::free(t);
            keep = false;
        }

        if (keep) tasks_[wr++] = t;
    }
    task_count_ = wr;
    current_index_ = 0;
    for (uint64_t i = 0; i < task_count_; ++i) {
        if (tasks_[i] == shell) {
            current_index_ = i;
            break;
        }
    }
}

static void switch_to_task(TaskControlBlock* current, TaskControlBlock* next) {
    if (!validate_switch(current, next, "switch")) {
        report_corruption("switch");
        return;
    }
    if (next->state != TaskState::READY && next->state != TaskState::RUNNING) {
        report_corruption("switch next state");
        return;
    }
    if (current == next) {
        return;
    }
#ifdef CONFIG_DEBUG
    {
        auto& ring = current->debug_switch_ring;
        auto& idx  = current->debug_switch_idx;
        auto& rec  = ring[idx % TaskControlBlock::DEBUG_SWITCH_RING_SIZE];
        rec.entry_addr     = reinterpret_cast<uint64_t>(__builtin_return_address(0));
#if defined(CONFIG_ARCH_X86_64)
        rec.exit_rip = current->context.rip;
#elif defined(CONFIG_ARCH_AARCH64)
        rec.exit_rip = current->context.elr_el1;
#endif
        rec.regs           = current->context;
        rec.thread_id      = current->id;
        rec.consumed_ticks = current->executed_ticks;
        ++idx;
    }
#endif
    __atomic_store_n(&scheduler_load_rsp_from, TASK_STACK_PTR(next), __ATOMIC_RELEASE);
    if (next->page_table_) {
        __atomic_store_n(&scheduler_load_cr3_from, next->page_table_, __ATOMIC_RELEASE);
#if defined(CONFIG_ARCH_X86_64)
        arch::GDT::set_tss_rsp0(next->kernel_stack_top);
#endif
    } else {
        __atomic_store_n(&scheduler_load_cr3_from, VMM::get_kernel_pml4(), __ATOMIC_RELEASE);
    }
    if (current->state == TaskState::RUNNING) {
        current->state = TaskState::READY;
        Scheduler::enqueue_ready(*current);
    }
    next->state = TaskState::RUNNING;
    __atomic_store_n(&scheduler_next_task_id, next->id, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_save_rsp_to, &TASK_STACK_PTR(current), __ATOMIC_RELEASE);

    uint64_t cr0 = arch::read_cr0();
    cr0 |= (1ULL << 3);
    arch::write_cr0(cr0);
}

void Scheduler::rate_monotonic_schedule() noexcept {
    if (task_count_ <= 1) return;

    if (!scheduler_lock_.try_lock()) {
        return;
    }

    if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != 0) {
        scheduler_lock_.unlock();
        return;
    }

    auto* current = tasks_[current_index_];
    if (!current || current->magic != TaskControlBlock::TCB_MAGIC) {
        scheduler_lock_.unlock();
        return;
    }

    auto* next = next_task();
    if (next && next != current) {
        switch_to_task(current, next);
    }

    scheduler_lock_.unlock();
}

void Scheduler::reschedule() noexcept {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    if (task_count_ <= 1) return;

    auto* current = tasks_[current_index_];
    if (!current || current->magic != TaskControlBlock::TCB_MAGIC) return;

    auto* next = next_task();
    if (next && next != current) {
        if (next->state != TaskState::READY && next->state != TaskState::RUNNING) {
            return;
        }
        switch_to_task(current, next);
    }
}

// ---------------------------------------------------------------------------
// Test-isolation helpers
// ---------------------------------------------------------------------------

void Scheduler::capture_state(TaskControlBlock** tasks_out,
                              TaskControlBlock** id_table_out,
                              uint64_t& task_count_out,
                              uint64_t& current_idx_out,
                              uint64_t& next_id_out,
                              TaskControlBlock*& idle_out,
                              bool& preempt_out,
                              uint64_t* rq_bitmap_hi,
                              uint64_t* rq_bitmap_lo,
                              uint64_t* sporadic_count_out) {
    __builtin_memcpy(tasks_out, tasks_,
                     sizeof(TaskControlBlock*) * MAX_TASKS);
    __builtin_memcpy(id_table_out, id_table_,
                     sizeof(TaskControlBlock*) * ID_TABLE_SIZE);
    task_count_out    = task_count_;
    current_idx_out   = current_index_;
    next_id_out       = next_task_id_;
    idle_out          = idle_task_;
    preempt_out       = preempt_enabled_;
    if (rq_bitmap_hi)  *rq_bitmap_hi  = ready_queue_.bitmap().raw_hi();
    if (rq_bitmap_lo)  *rq_bitmap_lo  = ready_queue_.bitmap().raw_lo();
    if (sporadic_count_out) *sporadic_count_out = sporadic_task_count_;
}

void Scheduler::restore_state(TaskControlBlock* const* tasks_in,
                              TaskControlBlock* const* id_table_in,
                              uint64_t task_count_in,
                              uint64_t current_idx_in,
                              uint64_t next_id_in,
                              TaskControlBlock* idle_in,
                              bool preempt_in,
                              uint64_t rq_bitmap_hi,
                              uint64_t rq_bitmap_lo,
                              uint64_t sporadic_count_in) {
    __builtin_memcpy(tasks_, tasks_in,
                     sizeof(TaskControlBlock*) * MAX_TASKS);
    __builtin_memcpy(id_table_, id_table_in,
                     sizeof(TaskControlBlock*) * ID_TABLE_SIZE);
    task_count_          = task_count_in;
    current_index_       = current_idx_in;
    next_task_id_        = next_id_in;
    idle_task_           = idle_in;
    preempt_enabled_     = preempt_in;
    sporadic_task_count_ = sporadic_count_in;

    // Rebuild ready queue from restored tasks and restore bitmap
    ready_queue_.reset();
    for (uint64_t i = 0; i < task_count_; ++i) {
        auto* t = tasks_[i];
        if (t) {
            t->in_ready_queue_ = false;
            if (t->magic == TaskControlBlock::TCB_MAGIC &&
                t->state == TaskState::READY) {
                ready_queue_.enqueue(*t, effective_priority(t));
            }
        }
    }
    ready_queue_.restore_state(rq_bitmap_hi, rq_bitmap_lo);

    // Validate RSP for tasks actually in ready queues (not all READY tasks)
    // to catch stale RSP from tasks that were RUNNING at snapshot time.
    for (uint64_t prio = 0; prio <= CONFIG_PRIORITY_CEILING; ++prio) {
        auto& q = ready_queue_.queue(prio);
        for (auto* t = q.head(); t; t = t->runq_next_) {
            if (t->magic != TaskControlBlock::TCB_MAGIC) continue;
            if (!rsp_in_stack_range(TASK_STACK_PTR(t), t, "restore")) {
                Logger::raw_write("[SCHED] restore: RSP corruption for task ");
                Logger::print_hex(t->id);
                Logger::raw_write(" (rsp=0x");
                Logger::print_hex(TASK_STACK_PTR(t));
                Logger::raw_write("), resetting to stack top\n");
                TASK_STACK_PTR(t) = t->kernel_stack_top;
            }
        }
    }
}

} // namespace kernel

extern "C" void scheduler_diag_pre_save() {
#ifdef CONFIG_DEBUG
    uint64_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    auto* cur = kernel::Scheduler::current_task();
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
            kernel::Logger::raw_write("]\n");
        } else {
            static uint64_t diag_cnt = 0;
            if (++diag_cnt % 1000 == 0) {
                kernel::Logger::raw_write("[DIAG] pre-save: idx=");
                kernel::Logger::print_dec(cidx);
                kernel::Logger::raw_write(" id=");
                kernel::Logger::print_dec(cur->id);
                kernel::Logger::raw_write(" rsp=0x");
                kernel::Logger::print_hex(rsp);
                kernel::Logger::raw_write("\n");
            }
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

// --- Error-returning overloads ---
namespace kernel {
 
using namespace errors;
 
SchedulerError Scheduler::init_err() {
    init();
    return SCHED_ERR_OK;
}
 
SchedulerError Scheduler::add_task_err(TaskControlBlock& task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    if (task_count_ >= MAX_TASKS) return SCHED_ERR_TABLE_FULL;
    if (id_table_find(task.id) != nullptr) return SCHED_ERR_DUPLICATE_ID;
    tasks_[task_count_++] = &task;
    id_table_insert(task.id, &task);
    ready_queue_.enqueue(task, effective_priority(&task));
    kernel::test::ResourceTracker::instance().track_task_add();
    return SCHED_ERR_OK;
}

SchedulerError Scheduler::remove_task_err(TaskControlBlock& task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    if (id_table_find(task.id) == nullptr) return SCHED_ERR_NOT_FOUND;
    id_table_remove(&task);
    dequeue_ready(task);
    kernel::test::ResourceTracker::instance().track_task_remove();
    for (uint64_t i = 0; i < task_count_; ++i) {
        if (tasks_[i] == &task) {
            tasks_[i] = tasks_[--task_count_];
            if (current_index_ == task_count_) {
                current_index_ = (i < task_count_) ? i : 0;
            } else if (current_index_ >= task_count_) {
                current_index_ = 0;
            }
            break;
        }
    }
    return SCHED_ERR_OK;
}
 
SchedulerError Scheduler::alloc_id_err(uint64_t& out_id) {
    // ID allocation never fails in current implementation (64-bit counter)
    // But we keep the error return for API consistency
    out_id = next_task_id_++;
    return SCHED_ERR_OK;
}
 
} // namespace kernel
 
extern "C" void scheduler_on_context_switch() {
    uint64_t id = __atomic_load_n(&kernel::scheduler_next_task_id, __ATOMIC_ACQUIRE);
    if (id == 0) return;
    __atomic_store_n(&kernel::scheduler_next_task_id, (uint64_t)0, __ATOMIC_RELEASE);
    kernel::Scheduler::set_current_by_id(id);
}
