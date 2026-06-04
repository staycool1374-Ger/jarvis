#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <assert.hpp>
#include <string.hpp>

namespace kernel {

static uint64_t next_task_id = 1;

TaskControlBlock* TaskControlBlock::create(
    void (*entry)(),
    uint64_t priority,
    uint64_t period_ticks)
{
    auto* tcb = new TaskControlBlock{};
    ASSERT(tcb != nullptr);

    tcb->id = next_task_id++;
    tcb->state = TaskState::READY;
    tcb->priority = priority;
    tcb->period_ticks = period_ticks;
    tcb->deadline_ticks = period_ticks;
    tcb->executed_ticks = 0;
    tcb->remaining_ticks = period_ticks;

    size_t stack_pages = (STACK_SIZE + 4095) / 4096;
    uint64_t stack_phys = PMM::alloc_contiguous(stack_pages);
    ASSERT(stack_phys != 0);

    tcb->stack_phys_ = stack_phys;
    tcb->kernel_stack = reinterpret_cast<uint8_t*>(stack_phys);
    tcb->kernel_stack_top = stack_phys + STACK_SIZE;

    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);

    *--stack = 0x10;
    *--stack = tcb->kernel_stack_top;

    *--stack = 0x200;
    *--stack = 0x08;
    *--stack = reinterpret_cast<uint64_t>(entry);

    *--stack = 0;
    *--stack = 0;

    for (int i = 0; i < 15; ++i) *--stack = 0;

    tcb->context.rsp = reinterpret_cast<uint64_t>(stack);

    return tcb;
}

void TaskControlBlock::save_context(uint64_t* rsp) noexcept {
    context.rsp = *rsp;
}

void TaskControlBlock::restore_context(uint64_t* rsp) noexcept {
    *rsp = context.rsp;
}

} // namespace kernel
