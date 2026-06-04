#include <kernel/task/scheduler.hpp>
#include <assert.hpp>

namespace kernel {

TaskControlBlock* Scheduler::tasks_[MAX_TASKS] = {};
uint64_t Scheduler::task_count_ = 0;
uint64_t Scheduler::current_index_ = 0;
bool Scheduler::preempt_enabled_ = false;
TaskControlBlock* Scheduler::idle_task_ = nullptr;

void Scheduler::init() {
    idle_task_ = TaskControlBlock::create([]() {
        while (true) {
            asm volatile("hlt");
        }
    }, 0, 0xFFFFFFFF);
    idle_task_->state = TaskState::READY;

    tasks_[0] = idle_task_;
    task_count_ = 1;
    current_index_ = 0;
    preempt_enabled_ = true;
}

void Scheduler::add_task(TaskControlBlock* task) {
    ASSERT(task_count_ < MAX_TASKS);
    tasks_[task_count_++] = task;
}

void Scheduler::remove_task(TaskControlBlock* task) {
    for (uint64_t i = 0; i < task_count_; ++i) {
        if (tasks_[i] == task) {
            tasks_[i] = tasks_[--task_count_];
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

    TaskControlBlock* best = tasks_[0];
    uint64_t best_priority = 0;

    for (uint64_t i = 0; i < task_count_; ++i) {
        auto* task = tasks_[i];
        if (task->state != TaskState::READY && task->state != TaskState::RUNNING) {
            continue;
        }
        if (task->priority > best_priority ||
            (task->priority == best_priority && task->period_ticks < best->period_ticks)) {
            best = task;
            best_priority = task->priority;
        }
    }
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

void Scheduler::on_tick() noexcept {
    if (!preempt_enabled_) return;

    for (uint64_t i = 0; i < task_count_; ++i) {
        auto* task = tasks_[i];
        if (task->state == TaskState::RUNNING || task->state == TaskState::READY) {
            ++task->executed_ticks;
            if (task->remaining_ticks > 0) --task->remaining_ticks;
        }
    }

    rate_monotonic_schedule();
}

void Scheduler::rate_monotonic_schedule() noexcept {
    if (task_count_ <= 1) return;

    auto* current = tasks_[current_index_];
    if (!current) return;

    auto* next = next_task();
    if (next && next != current) {
        scheduler_save_rsp_to = &current->context.rsp;
        scheduler_load_rsp_from = next->context.rsp;
        current->state = TaskState::READY;
        next->state = TaskState::RUNNING;
        set_current(next);
    }
}

} // namespace kernel
