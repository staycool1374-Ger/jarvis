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

/// @file timer.hpp
/// @brief PIT (Programmable Interval Timer) driver.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Timer interface used by the rest of the kernel.
/// @note Each architecture provides a concrete implementation matching
///       this interface.  The x86_64 implementation is PIT-based.
class Timer {
public:
    static void init(uint32_t frequency_hz);
    static uint64_t ticks();
    static void set_frequency(uint32_t frequency_hz);
    static void handle_irq();
    static void set_ticks_for_test(uint64_t value);

    static void oneshot(uint64_t ticks_from_now);
    static void periodic(uint64_t period_ticks);
    static uint64_t remaining();

private:
    static constexpr uint32_t PIT_BASE_FREQ = 1193182;
    static volatile uint64_t ticks_;
};

/// @brief HAL interface name for arch::Timer.
using ArchTimer = Timer;

} // namespace arch
