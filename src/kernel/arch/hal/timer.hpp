#pragma once

#include <types.hpp>

namespace arch {

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

using ArchTimer = Timer;

} // namespace arch
