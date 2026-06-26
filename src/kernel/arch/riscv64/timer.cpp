#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/idt.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

volatile uint64_t Timer::ticks_ = 0;
uint64_t Timer::timer_freq_hz_ = 0;

void Timer::init(uint32_t frequency_hz) {
    set_frequency(frequency_hz);
    IDT::register_handler(InterruptVector::TIMER, [](uint64_t, uint64_t, uint64_t) {
        handle_irq();
        kernel::Scheduler::on_tick();
    });
}

void Timer::set_frequency(uint32_t frequency_hz) {
    // Use SBI to get timer frequency (mtime typically 10MHz on QEMU)
    timer_freq_hz_ = 10000000;
    uint64_t interval = timer_freq_hz_ / frequency_hz;
    // SBI_SET_TIMER via ecall
    asm volatile("mv a0, %0; li a7, 0; ecall" : : "r"(interval) : "a0", "a7", "memory");
    // Enable STIP in sie
    uint64_t sie;
    asm volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1ULL << 5); // STIE
    asm volatile("csrw sie, %0" : : "r"(sie) : "memory");
}

uint64_t Timer::ticks() {
    return ticks_;
}

uint64_t Timer::ns() {
    if (timer_freq_hz_ == 0) return 0;
    uint64_t cnt;
    asm volatile("csrr %0, time" : "=r"(cnt));
    uint64_t sec = cnt / timer_freq_hz_;
    uint64_t rem = cnt % timer_freq_hz_;
    return sec * 1000000000ULL + (rem * 1000000000ULL) / timer_freq_hz_;
}

void Timer::handle_irq() {
    ticks_ = ticks_ + 1;
    // Re-arm timer via SBI
    uint64_t next = ticks_ * (timer_freq_hz_ / CONFIG_TICK_HZ);
    asm volatile("mv a0, %0; li a7, 0; ecall" : : "r"(next) : "a0", "a7", "memory");
}

void Timer::set_ticks_for_test(uint64_t value) {
    ticks_ = value;
}

void Timer::oneshot(uint64_t ticks_from_now) {
    uint64_t interval = ticks_from_now * (timer_freq_hz_ / 1000);
    if (interval == 0) interval = 1;
    uint64_t now;
    asm volatile("csrr %0, time" : "=r"(now));
    asm volatile("mv a0, %0; li a7, 0; ecall" : : "r"(now + interval) : "a0", "a7", "memory");
}

void Timer::periodic(uint64_t period_ticks) {
    uint64_t interval = timer_freq_hz_ / period_ticks;
    if (interval == 0) interval = 1;
    uint64_t now;
    asm volatile("csrr %0, time" : "=r"(now));
    asm volatile("mv a0, %0; li a7, 0; ecall" : : "r"(now + interval) : "a0", "a7", "memory");
}

uint64_t Timer::remaining() {
    // Cannot easily read mtimecmp from S-mode — return 0
    return 0;
}

} // namespace arch
