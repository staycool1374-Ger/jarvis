/// @file test_threaded_irqs.cpp
/// @brief Tests for deferred interrupt handling (v0.3.4).
///
/// STUB: The IrqThread class was implemented on `main` in commit bc7b190.
/// Once `testbed` is merged with `main`, replace stubs with:
///   - threaded_irq_create: create IrqThread, verify handler task runs
///   - threaded_irq_isr_latency: measure ISR latency with vs without threading
///   - threaded_irq_priority: verify IRQ thread runs at configured priority

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: STUB — IrqThread creation and handler execution.
// Depends: kernel::IrqThread (main branch)
JARVIS_TEST(threaded_irq_create, "PRE: none | POST: none") {
    /* Pseudocode:
     *   volatile bool handled = false;
     *   bool ok = IrqThread::create(TEST_VECTOR, 50,
     *       [&](uint64_t,uint64_t,uint64_t) { handled = true; });
     *   assert(ok);
     *   // trigger test vector via SW IRQ or timer
     *   wait for handled
     *   assert(handled);
     */
    Logger::warn("STUB: threaded_irq_create — IrqThread not yet on testbed");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB — ISR latency with threaded handler vs direct.
// Depends: kernel::IrqThread (main branch)
JARVIS_TEST(threaded_irq_isr_latency, "PRE: none | POST: none") {
    /* Pseudocode:
     *   t0 = rdtsc()
     *   trigger IRQ (or simulate)
     *   wait for threaded handler to run
     *   latency = rdtsc() - t0
     *   assert latency < THREADED_LATENCY_NS
     */
    Logger::warn("STUB: threaded_irq_isr_latency — IrqThread not yet on testbed");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB — IRQ thread priority enforcement.
// Depends: kernel::IrqThread (main branch)
JARVIS_TEST(threaded_irq_priority, "PRE: none | POST: none") {
    /* Pseudocode:
     *   IrqThread::create(vec, prio, handler)
     *   verify task->priority == prio
     *   run at different priorities, verify preemption behaviour
     */
    Logger::warn("STUB: threaded_irq_priority — IrqThread not yet on testbed");
    JARVIS_TEST_PASS();
}

void register_threaded_irq_tests() {
    Logger::info("Registering threaded IRQ tests (stubs)");
    JARVIS_REGISTER_TEST(threaded_irq_create);
    JARVIS_REGISTER_TEST(threaded_irq_isr_latency);
    JARVIS_REGISTER_TEST(threaded_irq_priority);
}
