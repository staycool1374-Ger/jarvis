#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

#if defined(CONFIG_ARCH_X86_64)

class Timer {
public:
    static void init(uint32_t frequency_hz);
    static uint64_t ticks();
    static uint64_t ns();
    static void set_frequency(uint32_t frequency_hz);
    static void handle_irq();
    static void set_ticks_for_test(uint64_t value);

    static void oneshot(uint64_t ticks_from_now);
    static void periodic(uint64_t period_ticks);
    static uint64_t remaining();

private:
    static void calibrate_tsc(uint32_t frequency_hz);
    static constexpr uint32_t PIT_BASE_FREQ = 1193182;
    static volatile uint64_t ticks_;
    static uint64_t tsc_freq_hz_;
};

#elif defined(CONFIG_ARCH_AARCH64)

class Timer {
public:
    static void init(uint32_t frequency_hz);
    static uint64_t ticks();
    static uint64_t ns();
    static void set_frequency(uint32_t frequency_hz);
    static void handle_irq();
    static void set_ticks_for_test(uint64_t value);

    static void oneshot(uint64_t ticks_from_now);
    static void periodic(uint64_t period_ticks);
    static uint64_t remaining();

private:
    static volatile uint64_t ticks_;
    static uint64_t counter_freq_hz_;
};

#else
#  error "HAL: no timer implementation for this architecture"
#endif

using ArchTimer = Timer;

} // namespace arch
