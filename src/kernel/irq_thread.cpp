#include <kernel/irq_thread.hpp>

#if CONFIG_THREADED_IRQS

#include <kernel/arch/interrupt_controller.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/apic.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>
#include <kernel/task/scheduler.hpp>
#include <logger.hpp>

namespace kernel {

// ─── Static storage ──────────────────────────────────────────────────────
IrqThread IrqThread::instances_[CONFIG_MAX_THREADED_IRQS];
size_t    IrqThread::count_ = 0;

// ─── Create ──────────────────────────────────────────────────────────────

bool IrqThread::create(uint8_t vector, uint64_t priority,
                       arch::ISRHandler handler,
                       void (*isr_ack)(uint8_t vector)) {
    // Idempotent per vector: a live instance already handles this vector
    // (e.g. the keyboard IrqThread re-created after reboot_from_table).
    for (size_t i = 0; i < count_; ++i) {
        if (instances_[i].valid_ && instances_[i].vector_ == vector) {
            Logger::info("IrqThread: vector %u already active, reusing", vector);
            return true;
        }
    }
    if (count_ >= CONFIG_MAX_THREADED_IRQS) {
        Logger::error("IrqThread: max instances (%u) reached", CONFIG_MAX_THREADED_IRQS);
        return false;
    }
    if (!handler) {
        Logger::error("IrqThread: null handler for vector %u", vector);
        return false;
    }

    // Create the handler task (kernel task, no period)
    auto *tcb = TaskControlBlock::create(task_entry, priority, 0);
    if (!tcb) {
        Logger::error("IrqThread: failed to create task for vector %u", vector);
        return false;
    }

    auto &inst = instances_[count_];
    inst.vector_   = vector;
    inst.priority_ = priority;
    inst.handler_  = handler;
    inst.isr_ack_  = isr_ack;
    inst.tcb_      = tcb;
    inst.notify_   = &tcb->notify;
    inst.ring_.reset();
    inst.valid_ = true;

    count_++;

    // Register the handler task with the scheduler so it can run task_entry()
    // and reach notify_->wait().  Without this the task is created but never
    // scheduled — ISR notifies go nowhere and threaded IRQs never fire their
    // handler (dead keyboard input in the interactive shell).
    Scheduler::add_task(*tcb);

    Logger::info("IrqThread: vector %u created (prio=%lu, tcb=%x)",
                 vector, priority, tcb->id);
    return true;
}

// ─── ISR entry ───────────────────────────────────────────────────────────

void IrqThread::isr_entry(uint8_t vector, uint64_t error_code, uint64_t rip) {
    (void)error_code;
    (void)rip;

    auto *irqt = for_vector(vector);
    if (!irqt)
        return;

    // 1. Default ack: call the arch-generic interrupt controller EOI.
    //    On x86_64 this sends PIC EOI; APIC EOI is handled separately
    //    via a custom isr_ack in the keyboard (and future) registration.
    //    On AArch64 this calls GIC EOI; on RISC-V this calls PLIC complete.
    if (irqt->isr_ack_) {
        irqt->isr_ack_(vector);
    } else {
        arch::ArchInterruptController::eoi(vector);
    }

    // 2. Wake the handler task
    //    notify() is ISR-safe: it does an atomic hand-off and calls
    //    Scheduler::set_task_ready() which enqueues the task in the
    //    ready queue for the next context switch.
    if (irqt->notify_) {
        irqt->notify_->notify(1);
    }
}

// ─── Find ────────────────────────────────────────────────────────────────

IrqThread *IrqThread::for_vector(uint8_t vector) {
    for (size_t i = 0; i < count_; ++i) {
        if (instances_[i].valid_ && instances_[i].vector_ == vector)
            return &instances_[i];
    }
    return nullptr;
}

bool IrqThread::is_irq_thread_task(const TaskControlBlock *t) noexcept {
    if (!t)
        return false;
    for (size_t i = 0; i < count_; ++i) {
        if (instances_[i].valid_ && instances_[i].tcb_ == t)
            return true;
    }
    return false;
}

// ─── Push data from ISR ─────────────────────────────────────────────────

bool IrqThread::try_push_data(const uint8_t *data, size_t len) {
    // Push byte by byte into the SPSCRing.
    // SPSCRing::try_push is lock-free and ISR-safe.
    for (size_t i = 0; i < len; ++i) {
        if (!ring_.try_push(data[i])) {
            // Ring full — discard remaining data
            return false;
        }
    }
    return true;
}

// ─── Task entry ─────────────────────────────────────────────────────────

void IrqThread::task_entry() {
    // Find the IrqThread instance that owns this task.
    // We match by current_task()->notify pointer.
    auto *self = Scheduler::current_task();
    IrqThread *irqt = nullptr;
    for (size_t i = 0; i < count_; ++i) {
        if (instances_[i].valid_ && instances_[i].tcb_ == self) {
            irqt = &instances_[i];
            break;
        }
    }

    if (!irqt) {
        Logger::error("IrqThread::task_entry: orphan task %x", self->id);
        Scheduler::terminate(*self, 1);
        __builtin_unreachable();
    }

    Logger::debug("IrqThread: task %x running handler for vector %u",
                  self->id, irqt->vector_);

    for (;;) {
        // Wait for the ISR to notify us
        uint64_t val = irqt->notify_->wait();
        (void)val;

        // Call the registered handler (runs in task context — can block)
        irqt->handler_(irqt->vector_, 0, 0);
    }
}

} // namespace kernel

#endif // CONFIG_THREADED_IRQS
