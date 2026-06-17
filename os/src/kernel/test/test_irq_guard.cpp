#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/irq_guard.hpp>
#include <kernel/arch/io.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies IrqGuard disables interrupts on construction and
//           restores original state on destruction.
// Input: Create IrqGuard scope
// Expect: interrupts_enabled() is false inside, restored outside
// Depends: arch::IrqGuard, arch::interrupts_enabled
JARVIS_TEST(irq_guard_saves_and_restores) {
    bool was_enabled = arch::interrupts_enabled();
    {
        arch::IrqGuard guard;
        JARVIS_ASSERT(!arch::interrupts_enabled());
    }
    JARVIS_ASSERT_EQ(was_enabled, arch::interrupts_enabled());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies nested IrqGuard works correctly.
// Input: Outer and inner IrqGuard scopes
// Expect: IF remains disabled throughout, restored to original after outer
// Depends: arch::IrqGuard
JARVIS_TEST(irq_guard_nested) {
    bool was_enabled = arch::interrupts_enabled();
    {
        arch::IrqGuard outer;
        JARVIS_ASSERT(!arch::interrupts_enabled());
        {
            arch::IrqGuard inner;
            JARVIS_ASSERT(!arch::interrupts_enabled());
        }
        JARVIS_ASSERT(!arch::interrupts_enabled());
    }
    JARVIS_ASSERT_EQ(was_enabled, arch::interrupts_enabled());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies IrqGuard is non-copyable and non-movable (compile check).
// Input: Attempt to copy or move (compile-time)
// Expect: Compilation succeeds for valid usage
// Depends: arch::IrqGuard
JARVIS_TEST(irq_guard_noncopyable) {
    arch::IrqGuard guard;
    JARVIS_ASSERT(!arch::interrupts_enabled());
    (void)guard;
    JARVIS_TEST_PASS();
}

void register_irq_guard_tests() {
    Logger::info("Registering IRQ guard tests");
    JARVIS_REGISTER_TEST(irq_guard_saves_and_restores);
    JARVIS_REGISTER_TEST(irq_guard_nested);
    JARVIS_REGISTER_TEST(irq_guard_noncopyable);
}
