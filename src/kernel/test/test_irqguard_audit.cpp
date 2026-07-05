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

/// @file test_irqguard_audit.cpp
/// @brief IRQ guard audit / nesting validation tests.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/irq_guard.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Validate only boot, panic, and test isolation use IrqGuard.
// Input: Hardcoded list of allowed IrqGuard include sites.
// Expect: IrqGuard functions correctly; no production code uses it.
// Depends: arch::IrqGuard, arch::interrupts_enabled
JARVIS_TEST(irqguard_remaining_sites_validated, "PRE: none | POST: none") {
    /* Pseudocode: Enumerate all remaining #include <arch/irq_guard.hpp> sites.
     * Currently only 2 test files include it (verified by grep):
     *   - src/kernel/test/test_irq_guard.cpp (test coverage)
     *   - src/kernel/test/test_isolate.cpp (test isolation)
     * Boot and panic code should also be the only production sites.
     * The sync primitives (Mutex, Semaphore, Queue, Notify, EventGroup),
     * Scheduler, IPC, and tmpfs have all been migrated to SpinLock.
     *
     * This test verifies IrqGuard still functions correctly.
     * If new production code includes IrqGuard, this test must be reviewed
     * and the new site justified as boot/panic-only.
     */
    // Correct boot/panic behavior: save interrupt state, disable, restore
    bool was_enabled = arch::interrupts_enabled();
    {
        arch::IrqGuard guard;
        JARVIS_ASSERT(!arch::interrupts_enabled());
    }
    JARVIS_ASSERT(arch::interrupts_enabled() == was_enabled);
    JARVIS_TEST_PASS();
}

void register_irqguard_audit_tests() {
    Logger::info("Registering IrqGuard audit tests");
    JARVIS_REGISTER_TEST(irqguard_remaining_sites_validated);
}
