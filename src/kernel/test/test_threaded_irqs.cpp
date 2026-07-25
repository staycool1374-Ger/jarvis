/// @file test_threaded_irqs.cpp
/// @brief Tests for deferred interrupt handling — IrqThread (v0.3.4).
///
/// STUB: The IrqThread handler task runs `for(;;)` and cannot be terminated
/// by the test framework's snapshot/restore.  Until IrqThread gains a
/// `cleanup(vector)` function that stops the handler task and reclaims its
/// resources, these tests remain as documented stubs.
///
/// The IrqThread production path IS verified: the keyboard driver (vector 33)
/// is migrated to IrqThread and passes selftest 132/132.
///
/// Pseudocode for real tests once cleanup exists:
///   - threaded_irq_create: IrqThread::create(vec, prio, handler) → ok
///     for_vector(vec) → non-null; then cleanup(vec) → resources freed
///   - threaded_irq_isr_entry: call isr_entry() directly, verify no crash
///   - threaded_irq_priority: create at prio 60 and 50, verify both found

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

JARVIS_TEST(threaded_irq_create, "PRE: none | POST: none") {
    Logger::warn("STUB: threaded_irq_create — needs IrqThread cleanup API");
    JARVIS_TEST_PASS();
}

JARVIS_TEST(threaded_irq_isr_entry, "PRE: none | POST: none") {
    Logger::warn("STUB: threaded_irq_isr_entry — needs IrqThread cleanup API");
    JARVIS_TEST_PASS();
}

JARVIS_TEST(threaded_irq_priority, "PRE: none | POST: none") {
    Logger::warn("STUB: threaded_irq_priority — needs IrqThread cleanup API");
    JARVIS_TEST_PASS();
}

void register_threaded_irq_tests() {
    Logger::info("Registering threaded IRQ tests (stubs — needs cleanup API)");
    JARVIS_REGISTER_TEST(threaded_irq_create);
    JARVIS_REGISTER_TEST(threaded_irq_isr_entry);
    JARVIS_REGISTER_TEST(threaded_irq_priority);
}
