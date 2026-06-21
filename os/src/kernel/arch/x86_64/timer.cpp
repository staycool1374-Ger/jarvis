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

#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/idt.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/arch/idt.hpp>

namespace arch {

volatile uint64_t Timer::ticks_ = 0;

void Timer::init(uint32_t frequency_hz) {
    set_frequency(frequency_hz);
    IDT::register_handler(InterruptVector::TIMER, [](uint64_t, uint64_t,
        uint64_t) {
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

void Timer::set_ticks_for_test(uint64_t value) {
    ticks_ = value;
}

} // namespace arch
