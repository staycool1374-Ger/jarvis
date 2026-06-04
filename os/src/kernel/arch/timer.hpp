/// @file timer.hpp
/// @brief PIT (Programmable Interval Timer) driver.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief PIT-based timer driver providing periodic ticks.
class Timer {
public:
    /// @brief Initialises the PIT with the given frequency.
    /// @param frequency_hz Desired interrupt frequency in Hz.
    static void init(uint32_t frequency_hz);
    /// @brief Returns the total tick count since initialisation.
    /// @return Tick count.
    static uint64_t ticks();

    /// @brief Changes the PIT frequency at runtime.
    /// @param frequency_hz New frequency in Hz.
    static void set_frequency(uint32_t frequency_hz);

    /// @brief Handles the timer IRQ (increments tick count).
    static void handle_irq();

private:
    static constexpr uint32_t PIT_BASE_FREQ = 1193182;
    static volatile uint64_t ticks_;
};

} // namespace arch
