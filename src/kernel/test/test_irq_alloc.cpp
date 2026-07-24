/// @file test_irq_alloc.cpp
/// @brief Tests for allocation-free IRQ paths (v0.3.4).
///
/// STUB: These tests verify that no dynamic allocation occurs in IRQ handlers
/// or syscall fast-paths.  They require instrumentation hooks
/// (CONFIG_MEMPOOL_TRACK_ALLOC or similar) that were discussed for v0.3.4
/// but not implemented on `testbed`.  Once the instrumentation is added on
/// `main` and merged, replace stubs with:
///   - irq_alloc_no_alloc_in_timer_isr
///   - irq_alloc_no_alloc_in_keyboard_isr
///   - irq_alloc_no_alloc_in_syscall_fastpath

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: STUB — No allocation in timer interrupt handler.
// Depends: CONFIG_MEMPOOL_TRACK_ALLOC (main branch)
JARVIS_TEST(irq_alloc_no_alloc_in_timer_isr, "PRE: none | POST: none") {
    /* Pseudocode:
     *   snapshot MemPool free count
     *   let N timer ticks pass
     *   assert MemPool free count unchanged
     */
    Logger::warn("STUB: irq_alloc_no_alloc_in_timer_isr — not yet on testbed");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB — No allocation in keyboard ISR.
// Depends: CONFIG_MEMPOOL_TRACK_ALLOC (main branch)
JARVIS_TEST(irq_alloc_no_alloc_in_keyboard_isr, "PRE: none | POST: none") {
    /* Pseudocode:
     *   snapshot MemPool free count
     *   inject scancode via keyboard ISR
     *   assert MemPool free count unchanged
     */
    Logger::warn("STUB: irq_alloc_no_alloc_in_keyboard_isr — not yet on testbed");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB — No allocation in syscall fast-path (getpid, getticks).
// Depends: CONFIG_MEMPOOL_TRACK_ALLOC (main branch)
JARVIS_TEST(irq_alloc_no_alloc_in_syscall_fastpath, "PRE: none | POST: none") {
    /* Pseudocode:
     *   snapshot MemPool free count
     *   call getpid, getticks via syscall
     *   assert MemPool free count unchanged
     */
    Logger::warn("STUB: irq_alloc_no_alloc_in_syscall_fastpath — not yet on testbed");
    JARVIS_TEST_PASS();
}

void register_irq_alloc_tests() {
    Logger::info("Registering IRQ allocation tests (stubs)");
    JARVIS_REGISTER_TEST(irq_alloc_no_alloc_in_timer_isr);
    JARVIS_REGISTER_TEST(irq_alloc_no_alloc_in_keyboard_isr);
    JARVIS_REGISTER_TEST(irq_alloc_no_alloc_in_syscall_fastpath);
}
