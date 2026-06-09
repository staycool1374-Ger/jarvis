#include <kernel/task/scheduler.hpp>
#include <kernel/arch/gdt.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/memory/vmm.hpp>
#include <signal.hpp>
#include <assert.hpp>

namespace kernel {

TaskControlBlock* const Scheduler::ID_TOMBSTONE =
    reinterpret_cast<TaskControlBlock*>(static_cast<uintptr_t>(1));

TaskControlBlock* Scheduler::tasks_[MAX_TASKS] = {};
TaskControlBlock* Scheduler::id_table_[ID_TABLE_SIZE] = {};
uint64_t Scheduler::task_count_ = 0;
uint64_t Scheduler::current_index_ = 0;
bool Scheduler::preempt_enabled_ = false;
TaskControlBlock* Scheduler::idle_task_ = nullptr;

void Scheduler::init() {
    for (uint64_t i = 0; i < ID_TABLE_SIZE; ++i) {
        id_table_[i] = nullptr;
    }

    idle_task_ = TaskControlBlock::create([]() {
        while (true) {
            arch::hlt();
        }
    }, 0, 0xFFFFFFFF);
    idle_task_->state = TaskState::READY;

    tasks_[0] = idle_task_;
    id_table_insert(idle_task_->id, idle_task_);
    task_count_ = 1;
    current_index_ = 0;
    preempt_enabled_ = true;
}

void Scheduler::add_task(TaskControlBlock* task) {
    ASSERT(task_count_ < MAX_TASKS);
    tasks_[task_count_++] = task;
    id_table_insert(task->id, task);
}

void Scheduler::remove_task(TaskControlBlock* task) {
    id_table_remove(task->id);
    for (uint64_t i = 0; i < task_count_; ++i) {
        if (tasks_[i] == task) {
            tasks_[i] = tasks_[--task_count_];
            if (current_index_ >= task_count_) current_index_ = 0;
            if (i < task_count_ && current_index_ == task_count_) current_index_ = i;
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
        if (task->state == TaskState::READY || task->state == TaskState::RUNNING) {
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
        if (task->state != TaskState::READY && task->state != TaskState::RUNNING) continue;
        if (task->priority > best_priority ||
            (task->priority == best_priority && task->period_ticks < best_period) ||
            (task->priority == best_priority && task->period_ticks == best_period && task != current)) {
            best = task;
            best_priority = task->priority;
            best_period = task->period_ticks;
        }
    }

    if (!best) return tasks_[0];
    return best;
}

void Scheduler::set_current(TaskControlBlock* task) noexcept {
    for (uint64_t i = 0; i < task_count_; ++i) {
        if (tasks_[i] == task) {
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

void Scheduler::id_table_remove(uint64_t id) {
    uint64_t idx = id_table_probe(id);
    while (id_table_[idx] != nullptr) {
        if (id_table_[idx] != ID_TOMBSTONE && id_table_[idx]->id == id) {
            id_table_[idx] = const_cast<TaskControlBlock*>(ID_TOMBSTONE);
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
        if (task->state == TaskState::RUNNING || task->state == TaskState::READY) {
            ++task->executed_ticks;
            if (task->remaining_ticks > 0) --task->remaining_ticks;

            // Check for alarm expiration
            if (task->alarm_armed && current_tick >= task->alarm_ticks) {
                task->alarm_armed = false;
                // Deliver SIGALRM to the task
                task->pending_signals |= (1ULL << static_cast<uint64_t>(Signal::SIGALRM));
            }
        }
    }

    static uint64_t tick_counter = 0;
    ++tick_counter;
    if (tick_counter % 100 == 0) {
        reap_orphans();
    }

    rate_monotonic_schedule();
}

void Scheduler::reap_orphans() noexcept {
    auto* current = current_task();
    bool reaped_any = true;
    while (reaped_any) {
        reaped_any = false;
        for (uint64_t i = 0; i < task_count_; ++i) {
            auto* t = tasks_[i];
            if (!t || t == current) continue;
            if (t->state != TaskState::TERMINATED) continue;

            bool can_reap = false;
            if (t->parent_id == 0) {
                can_reap = true;
            } else {
                bool parent_alive = false;
                for (uint64_t j = 0; j < task_count_; ++j) {
                    auto* p = tasks_[j];
                    if (!p) continue;
                    if (p->id == t->parent_id) {
                        if (p->state != TaskState::TERMINATED) {
                            parent_alive = true;
                        }
                        break;
                    }
                }
                if (!parent_alive) can_reap = true;
            }

            if (can_reap) {
                // If this task owns its page tables (not shared with a parent)
                // and has TERMINATED children that share those tables,
                // reap the children first so they don't reference freed pages.
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

                t->cleanup();
                remove_task(t);
                delete t;
                reaped_any = true;
                break;
            }
        }
    }
}

static void switch_to_task(TaskControlBlock* current, TaskControlBlock* next) {
    if (next->state != TaskState::READY && next->state != TaskState::RUNNING) {
        return;
    }
    scheduler_save_rsp_to = &current->context.rsp;
    scheduler_load_rsp_from = next->context.rsp;
    if (next->page_table_) {
        scheduler_load_cr3_from = next->page_table_;
        arch::GDT::set_tss_rsp0(next->kernel_stack_top);
    } else {
        scheduler_load_cr3_from = VMM::get_kernel_pml4();
    }
    if (current->state == TaskState::RUNNING) {
        current->state = TaskState::READY;
    }
    next->state = TaskState::RUNNING;
    Scheduler::set_current(next);
}

void Scheduler::rate_monotonic_schedule() noexcept {
    if (task_count_ <= 1) return;

    auto* current = tasks_[current_index_];
    if (!current) return;

    auto* next = next_task();
    if (next && next != current) {
        switch_to_task(current, next);
    }
}

void Scheduler::reschedule() noexcept {
    if (task_count_ <= 1) return;

    auto* current = tasks_[current_index_];
    if (!current) return;

    auto* next = next_task();
    if (next && next != current) {
        if (next->state != TaskState::READY && next->state != TaskState::RUNNING) {
            return;
        }
        switch_to_task(current, next);
    }
}

} // namespace kernel
