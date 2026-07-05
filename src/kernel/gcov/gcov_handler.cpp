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

/// @file gcov_handler.cpp
/// @brief GCOV profiling support — function-entry instrumentation and serial flush.

#include <types.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>

extern "C" {

#define MAX_FUNCTIONS 4096

static uint64_t called_functions[MAX_FUNCTIONS / 64];
static uint64_t func_addrs[MAX_FUNCTIONS];
static uint32_t func_count = 0;

/// @brief Find or register a function address in the profiling table.
/// @param func_addr  Address of the function being tracked.
/// @return Index into func_addrs / called_functions arrays.
static uint32_t __attribute__((no_instrument_function))
find_or_add_func(uint64_t func_addr) {
    for (uint32_t i = 0; i < func_count; i++) {
        if (func_addrs[i] == func_addr)
            return i;
    }
    if (func_count < MAX_FUNCTIONS) {
        func_addrs[func_count] = func_addr;
        return func_count++;
    }
    return 0;
}

/// @brief GCC instrumentation hook — called on every function entry.
/// Marks the function as called in the called_functions bitmap.
/// @param func   Address of the entered function.
/// @param caller Address of the call site (unused).
void __attribute__((no_instrument_function))
__cyg_profile_func_enter(void *func, void *caller) { // NOLINT(bugprone-reserved-identifier, bugprone-easily-swappable-parameters)
    (void)caller;
    uint64_t addr = reinterpret_cast<uint64_t>(func);
    uint32_t idx = find_or_add_func(addr);
    called_functions[idx / 64] |= (1ULL << (idx % 64));
}

/// @brief GCC instrumentation hook — called on every function exit (no-op).
/// @param func   Address of the exited function (unused).
/// @param caller Address of the call site (unused).
void __attribute__((no_instrument_function))
__cyg_profile_func_exit(void *func, void *caller) { // NOLINT(bugprone-reserved-identifier, bugprone-easily-swappable-parameters)
    (void)func;
    (void)caller;
}

/// @brief Flush profiling data to the serial port.
/// Sends magic header 'FUNC', function count, and (addr, called) pairs
/// over COM1 for the host-side extract_gcda.py script to reassemble.
void __attribute__((no_instrument_function))
gcov_flush_to_serial() {
#if defined(CONFIG_ARCH_X86_64)
    uint8_t magic[4] = {'F', 'U', 'N', 'C'};
    for (int i = 0; i < 4; ++i) {
        while ((arch::inb(arch::COM1 + 5) & 0x20) == 0);
        arch::outb(arch::COM1, magic[i]);
    }

    uint32_t total = func_count;
    for (int i = 0; i < 4; ++i) {
        while ((arch::inb(arch::COM1 + 5) & 0x20) == 0);
        arch::outb(arch::COM1, (total >> (i * 8)) & 0xFF);
    }

    for (uint32_t i = 0; i < func_count; ++i) {
        uint64_t addr = func_addrs[i];
        uint32_t called = (called_functions[i / 64] >> (i % 64)) & 1;
        for (int j = 0; j < 8; ++j) {
            while ((arch::inb(arch::COM1 + 5) & 0x20) == 0);
            arch::outb(arch::COM1, (addr >> (j * 8)) & 0xFF);
        }
        while ((arch::inb(arch::COM1 + 5) & 0x20) == 0);
        arch::outb(arch::COM1, called & 0xFF);
    }
#else
    (void)func_count; (void)func_addrs; (void)called_functions;
#endif
}

} // extern "C"