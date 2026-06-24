#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/idt.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

volatile uint64_t Timer::ticks_ = 0;
uint64_t Timer::counter_freq_hz_ = 0;

void Timer::init(uint32_t frequency_hz) {
    set_frequency(frequency_hz);
    IDT::register_handler(InterruptVector::TIMER, [](uint64_t, uint64_t, uint64_t) {
        handle_irq();
        kernel::Scheduler::on_tick();
    });
}

void Timer::set_frequency(uint32_t frequency_hz) {
    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    counter_freq_hz_ = freq;
    uint64_t interval = freq / frequency_hz;
    asm volatile("msr cntp_tval_el0, %0" : : "r"(interval));
    asm volatile("msr cntp_ctl_el0, %0" : : "r"(1UL));
    isb();
}

uint64_t Timer::ticks() {
    return ticks_;
}

uint64_t Timer::ns() {
    if (counter_freq_hz_ == 0) return 0;
    uint64_t cnt;
    asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    uint64_t sec = cnt / counter_freq_hz_;
    uint64_t rem = cnt % counter_freq_hz_;
    return sec * 1000000000ULL + (rem * 1000000000ULL) / counter_freq_hz_;
}

void Timer::handle_irq() {
    ticks_ = ticks_ + 1;
    asm volatile("msr cntp_ctl_el0, %0" : : "r"(1UL));
    isb();
}

void Timer::set_ticks_for_test(uint64_t value) {
    ticks_ = value;
}

void Timer::oneshot(uint64_t ticks_from_now) {
    uint64_t interval = ticks_from_now * (counter_freq_hz_ / 1000);
    if (interval == 0) interval = 1;
    asm volatile("msr cntp_tval_el0, %0" : : "r"(interval));
    asm volatile("msr cntp_ctl_el0, %0" : : "r"(1UL));
    isb();
}

void Timer::periodic(uint64_t period_ticks) {
    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    uint64_t interval = freq / period_ticks;
    if (interval == 0) interval = 1;
    asm volatile("msr cntp_tval_el0, %0" : : "r"(interval));
    asm volatile("msr cntp_ctl_el0, %0" : : "r"(1UL));
    isb();
}

uint64_t Timer::remaining() {
    uint64_t val;
    asm volatile("mrs %0, cntp_tval_el0" : "=r"(val));
    return val;
}

} // namespace arch
