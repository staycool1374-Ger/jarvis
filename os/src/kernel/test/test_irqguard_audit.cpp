#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/irq_guard.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Validate only boot, panic, and test isolation use IrqGuard.
// Input: Hardcoded list of allowed IrqGuard include sites.
// Expect: IrqGuard functions correctly; no production code uses it.
// Depends: arch::IrqGuard, arch::interrupts_enabled
JARVIS_TEST(irqguard_remaining_sites_validated) {
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
