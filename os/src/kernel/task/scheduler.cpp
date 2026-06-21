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
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/driver/iocd.hpp>
#include <kernel/test/resource_tracker.hpp>
#include <signal.hpp>
#include <logger.hpp>
#include <assert.hpp>

namespace kernel {

TaskControlBlock* const Scheduler::ID_TOMBSTONE =
    reinterpret_cast<TaskControlBlock*>(static_cast<uintptr_t>(1));

TaskControlBlock* Scheduler::tasks_[MAX_TASKS] = {};
TaskControlBlock* Scheduler::id_table_[ID_TABLE_SIZE] = {};
uint64_t Scheduler::task_count_ = 0;
uint64_t Scheduler::current_index_ = 0;
uint64_t Scheduler::next_task_id_ = 1;
bool Scheduler::preempt_enabled_ = false;
TaskControlBlock* Scheduler::idle_task_ = nullptr;
TaskControlBlock* Scheduler::shell_task_ptr_ = nullptr;
sync::SpinLock Scheduler::scheduler_lock_;

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
    preempt_enabled_ = true;
}

void Scheduler::add_task(TaskControlBlock& task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    ENSURE(task_count_ < MAX_TASKS);
    tasks_[task_count_++] = &task;
    id_table_insert(task.id, &task);
    kernel::test::ResourceTracker::instance().track_task_add();
}

void Scheduler::remove_task(TaskControlBlock& task) {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    id_table_remove(&task);
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
    if (!current || current == idle_task_) return true;

    for (uint64_t i = 1; i < task_count_; ++i) {
        auto* task = tasks_[i];
        if (task == current) continue;
        if (task->magic != TaskControlBlock::TCB_MAGIC) continue;
        if (task->state == TaskState::READY || task->state == TaskState::RUNNING
            ) {
            if (task->priority > current->priority) return true;
        }
    }
    return false;
}

TaskControlBlock* Scheduler::next_task() noexcept {
    if (task_count_ <= 1) return tasks_[0];

    auto* current = tasks_[current_index_];
    TaskControlBlock* best = nullptr;
    uint64_t best_priority = 0;
    uint64_t best_period = ~0ULL;

    for (uint64_t i = 0; i < task_count_; ++i) {
        auto* task = tasks_[i];
        if (task->magic != TaskControlBlock::TCB_MAGIC) continue;
        if (task->state != TaskState::READY && task->state != TaskState::RUNNING
            ) continue;
        if (task->priority > best_priority ||
            (task->priority == best_priority && task->period_ticks < best_period
                ) ||
            (task->priority == best_priority &&
                task->period_ticks == best_period && task != current)) {
            best = task;
            best_priority = task->priority;
            best_period = task->period_ticks;
        }
    }

    if (!best) return tasks_[0];
    return best;
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
        if (tasks_[i]->id == id) {
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
    if (!preempt_enabled_) return;

    uint64_t current_tick = arch::Timer::ticks();

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

    // If this tick is from a nested interrupt (ISR inside another ISR, e.g.
    // timer fired inside the syscall handler's sti()/hlt()/cli() window), skip
    // non-atomic scheduler operations that could corrupt state being modified
    // by the outer ISR (reschedule, task addition/removal).
    if (isr_nesting_depth > 1) return;

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
    bool reaped_any = true;
    while (reaped_any) {
        reaped_any = false;
        for (uint64_t i = 0; i < task_count_; ++i) {
            auto* t = tasks_[i];
            if (!t) continue;
            if (t->state != TaskState::TERMINATED) continue;
            if (t != idle_task_ && t == current) continue;

            TaskControlBlock* init_task = (task_count_ > 0) ? tasks_[0] :
                nullptr;

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
                for (uint64_t j = 0; j < task_count_; ++j) {
                    auto* c = tasks_[j];
                    if (!c || c == t || c == current) continue;
                    if (c == init_task) continue;
                    if (c->parent_id == t->id) {
                        c->parent_id = 0;
                        if (t->num_children > 0) --t->num_children;
                        init_task->add_child(c);
                    }
                }
            }

            // Determine if this task can be reaped
            bool can_reap = false;
            if (t->parent_id == 0) {
                // Root tasks (no parent, e.g. idle task) are always reapable
                can_reap = true;
            } else {
                bool parent_found = false;
                for (uint64_t j = 0; j < task_count_; ++j) {
                    auto* p = tasks_[j];
                    if (p && p->id == t->parent_id) {
                        parent_found = true;
                        // Can reap if parent is dead
                        if (p->state == TaskState::TERMINATED) {
                            can_reap = true;
                        }
// Can reap if parent is alive but no longer waiting for this child
// (parent must have explicitly called waitpid for a different child)
                        else if (p->waiting_child_pid != 0 &&
                                 p->waiting_child_pid != t->id &&
                                 p->waiting_child_pid != static_cast<uint64_t>(
                                     -1)) {
                            can_reap = true;
                        }
// Can reap if parent cleared its wait (collected status or was never waiting)
                        else if (p->waiting_child_pid == 0) {
                            can_reap = true;
                        }
                        break;
                    }
                }
// If parent was not found in the scheduler, task is an orphan -> reapable
                if (!parent_found) can_reap = true;
            }

            if (can_reap) {
                if (!t->page_table_shared_) {
                    bool has_sharing_child = false;
                    for (uint64_t j = 0; j < task_count_; ++j) {
                        auto* c = tasks_[j];
                        if (!c || c == t || c == current) continue;
                        if (c->parent_id == t->id && c->page_table_shared_) {
                            has_sharing_child = true;
                            break;
                        }
                    }
                    if (has_sharing_child) continue;
                }

                // If reaping the idle task, recreate it
                if (t == idle_task_) {
                    auto* new_idle = TaskControlBlock::create(kernel::integrity::idle_task_main, 0, 0xFFFFFFFF);
                    new_idle->state = TaskState::READY;
                    t->cleanup();
                    id_table_remove(t);
                    // Compact by shifting left past the removed old idle
                    for (uint64_t k = i; k + 1 < task_count_; ++k) {
                        tasks_[k] = tasks_[k + 1];
                    }
                    --task_count_;
                    MemPool::free(t);
                    // Shift right to make room for new idle at index 0
                    for (uint64_t k = task_count_; k > 0; --k) {
                        tasks_[k] = tasks_[k - 1];
                    }
                    tasks_[0] = new_idle;
                    ++task_count_;
                    current_index_ = 0;
                    idle_task_ = new_idle;
                    id_table_insert(idle_task_->id, idle_task_);
                } else {
                    t->cleanup();
                    remove_task(*t);
                    MemPool::free(t);
                }
                reaped_any = true;
                break;
            }
        }
    }
    inside_reap = false;
}

void Scheduler::cleanup_test_tasks() noexcept {
    for (uint64_t i = 1; i < task_count_; ++i) {
        auto* t = tasks_[i];
        if (t && t->state != TaskState::TERMINATED) {
            t->state = TaskState::TERMINATED;
            t->exit_code = 0;
        }
    }
    reap_orphans();
    uint64_t i = 1;
    while (i < task_count_) {
        auto* t = tasks_[i];
        if (t && t != idle_task_) {
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
    Logger::raw_write(": rsp=0x"); Logger::print_hex(rsp);
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
    if (!rsp_in_stack_range(next->context.rsp, next, label)) return false;
    return true;
}

void Scheduler::cleanup_zombies() noexcept {
    auto* shell = shell_task_ptr_;
    if (!shell || shell->magic != TaskControlBlock::TCB_MAGIC) {
        shell = current_task();
    }
    uint64_t vfsd_pid = vfsd::get_vfsd_pid();
    uint64_t iocd_pid = iocd::get_iocd_pid();

    uint64_t wr = 0;
    for (uint64_t rd = 0; rd < task_count_; ++rd) {
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
        rec.exit_rip       = current->context.rip;
        rec.regs           = current->context;
        rec.thread_id      = current->id;
        rec.consumed_ticks = current->executed_ticks;
        ++idx;
    }
#endif
    __atomic_store_n(&scheduler_load_rsp_from, next->context.rsp, __ATOMIC_RELEASE);
    if (next->page_table_) {
        __atomic_store_n(&scheduler_load_cr3_from, next->page_table_, __ATOMIC_RELEASE);
        arch::GDT::set_tss_rsp0(next->kernel_stack_top);
    } else {
        __atomic_store_n(&scheduler_load_cr3_from, VMM::get_kernel_pml4(), __ATOMIC_RELEASE);
    }
    if (current->state == TaskState::RUNNING) {
        current->state = TaskState::READY;
    }
    next->state = TaskState::RUNNING;
    __atomic_store_n(&scheduler_next_task_id, next->id, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_save_rsp_to, &current->context.rsp, __ATOMIC_RELEASE);

    uint64_t cr0 = arch::read_cr0();
    cr0 |= (1ULL << 3);
    arch::write_cr0(cr0);
}

void Scheduler::rate_monotonic_schedule() noexcept {
    if (task_count_ <= 1) return;

    auto* current = tasks_[current_index_];
    if (!current) return;

    auto* next = next_task();
    if (next && next != current) {
        Logger::raw_write("[SCHED] switch curr=");
        Logger::print_hex(current->id);
        Logger::raw_write(" next=");
        Logger::print_hex(next->id);
        Logger::raw_write(" cur_rsp=");
        Logger::print_hex(current->context.rsp);
        Logger::raw_write(" next_rsp=");
        Logger::print_hex(next->context.rsp);
        Logger::raw_write(" cur_pri=");
        Logger::print_dec(current->priority);
        Logger::raw_write(" next_pri=");
        Logger::print_dec(next->priority);
        Logger::raw_write(" nxt_state=");
        Logger::print_hex(static_cast<uint64_t>(next->state));
        Logger::raw_write(" cur_state=");
        Logger::print_hex(static_cast<uint64_t>(current->state));
        Logger::raw_write("\n");
        switch_to_task(current, next);
    }
}

void Scheduler::reschedule() noexcept {
    SpinLockGuard<sync::SpinLock> guard(scheduler_lock_);
    if (task_count_ <= 1) return;

    auto* current = tasks_[current_index_];
    if (!current) return;

    auto* next = next_task();
    if (next && next != current) {
        if (next->state != TaskState::READY && next->state != TaskState::RUNNING
            ) {
            return;
        }
        Logger::raw_write("[RSCHED] curr=");
        Logger::print_hex(current->id);
        Logger::raw_write(" next=");
        Logger::print_hex(next->id);
        Logger::raw_write(" cur_rsp=");
        Logger::print_hex(current->context.rsp);
        Logger::raw_write(" next_rsp=");
        Logger::print_hex(next->context.rsp);
        Logger::raw_write("\n");
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
                              bool& preempt_out) {
    __builtin_memcpy(tasks_out, tasks_,
                     sizeof(TaskControlBlock*) * MAX_TASKS);
    __builtin_memcpy(id_table_out, id_table_,
                     sizeof(TaskControlBlock*) * ID_TABLE_SIZE);
    task_count_out   = task_count_;
    current_idx_out  = current_index_;
    next_id_out      = next_task_id_;
    idle_out         = idle_task_;
    preempt_out      = preempt_enabled_;
}

void Scheduler::restore_state(TaskControlBlock* const* tasks_in,
                              TaskControlBlock* const* id_table_in,
                              uint64_t task_count_in,
                              uint64_t current_idx_in,
                              uint64_t next_id_in,
                              TaskControlBlock* idle_in,
                              bool preempt_in) {
    __builtin_memcpy(tasks_, tasks_in,
                     sizeof(TaskControlBlock*) * MAX_TASKS);
    __builtin_memcpy(id_table_, id_table_in,
                     sizeof(TaskControlBlock*) * ID_TABLE_SIZE);
    task_count_     = task_count_in;
    current_index_  = current_idx_in;
    next_task_id_   = next_id_in;
    idle_task_      = idle_in;
    preempt_enabled_ = preempt_in;
}

} // namespace kernel

extern "C" void scheduler_on_context_switch() {
    uint64_t id = __atomic_load_n(&kernel::scheduler_next_task_id, __ATOMIC_ACQUIRE);
    if (id == 0) return;
    __atomic_store_n(&kernel::scheduler_next_task_id, (uint64_t)0, __ATOMIC_RELEASE);
    kernel::Scheduler::set_current_by_id(id);
}
