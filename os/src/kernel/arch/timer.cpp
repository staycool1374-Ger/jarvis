#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/idt.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/arch/idt.hpp>

namespace arch {

volatile uint64_t Timer::ticks_ = 0;

void Timer::init(uint32_t frequency_hz) {
    set_frequency(frequency_hz);
    IDT::register_handler(InterruptVector::TIMER, [](uint64_t, uint64_t, uint64_t) {
        handle_irq();
        kernel::Scheduler::on_tick();
    });
}

void Timer::set_frequency(uint32_t frequency_hz) {
    uint32_t divisor = PIT_BASE_FREQ / frequency_hz;

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

uint64_t Timer::ticks() {
    return ticks_;
}

void Timer::handle_irq() {
    ticks_ = ticks_ + 1;
}

} // namespace arch
