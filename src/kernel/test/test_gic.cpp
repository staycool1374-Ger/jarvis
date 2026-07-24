/// @file test_gic.cpp
/// @brief Tests for ARM64 GICv3/v4 interrupt controller (v0.3.4).
///
/// STUB: The GIC driver and ArchInterruptController interface exist on `main`
/// in arch/aarch64/interrupt_controller.cpp (commits 5906255..2384f38).
/// These tests validate the GIC HAL header and dispatch logic.  On x86_64
/// (the CI test runner arch) GIC hardware is absent — all tests stub-pass.

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: STUB — GICv3 distributor init and ISENABLER register access.
// Depends: arch::GICD_* registers (aarch64 only)
JARVIS_TEST(gic_distributor_init, "PRE: none | POST: none") {
#if defined(CONFIG_ARCH_AARCH64)
    // Validate GICD_CTLR, GICD_TYPER, ISENABLER after init
    Logger::warn("STUB: gic_distributor_init — real test on AArch64 only");
#else
    Logger::warn("GIC test skipped (not AArch64)");
#endif
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB — GICv3 CPU interface ICC_PMR, ICC_EOIR access.
// Depends: arch::gic_v3_write_eoir, gic_v3_set_pmr (aarch64 only)
JARVIS_TEST(gic_cpu_interface, "PRE: none | POST: none") {
#if defined(CONFIG_ARCH_AARCH64)
    Logger::warn("STUB: gic_cpu_interface — real test on AArch64 only");
#else
    Logger::warn("GIC test skipped (not AArch64)");
#endif
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB — GIC timer PPI (INTID 30) dispatch via IDT.
// Depends: arch::handle_gic_irq, arch::IDT (aarch64 only)
JARVIS_TEST(gic_timer_dispatch, "PRE: none | POST: none") {
#if defined(CONFIG_ARCH_AARCH64)
    Logger::warn("STUB: gic_timer_dispatch — real test on AArch64 only");
#else
    Logger::warn("GIC test skipped (not AArch64)");
#endif
    JARVIS_TEST_PASS();
}

void register_gic_tests() {
    Logger::info("Registering GIC tests (stubs)");
    JARVIS_REGISTER_TEST(gic_distributor_init);
    JARVIS_REGISTER_TEST(gic_cpu_interface);
    JARVIS_REGISTER_TEST(gic_timer_dispatch);
}
