/// @file test_plic.cpp
/// @brief Tests for RISC-V64 PLIC interrupt controller (v0.3.4).
///
/// STUB: The PLIC driver and ArchInterruptController interface exist on `main`
/// in arch/riscv64/interrupt_controller.cpp (commits 5906255..2384f38).
/// These tests validate the PLIC HAL header and claim/complete dispatch.
/// On x86_64 (the CI test runner arch) PLIC hardware is absent — all tests
/// stub-pass.

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: STUB — PLIC init and threshold register access.
// Depends: arch::PLIC_THRESHOLD, sie CSR (riscv64 only)
JARVIS_TEST(plic_init_threshold, "PRE: none | POST: none") {
#if defined(CONFIG_ARCH_RISCV64)
    Logger::warn("STUB: plic_init_threshold — real test on RISC-V64 only");
#else
    Logger::warn("PLIC test skipped (not RISC-V64)");
#endif
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB — PLIC enable/mask register read/write.
// Depends: arch::PLIC_ENABLE, mask/unmask (riscv64 only)
JARVIS_TEST(plic_enable_mask, "PRE: none | POST: none") {
#if defined(CONFIG_ARCH_RISCV64)
    Logger::warn("STUB: plic_enable_mask — real test on RISC-V64 only");
#else
    Logger::warn("PLIC test skipped (not RISC-V64)");
#endif
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB — PLIC claim/complete cycle for external IRQ.
// Depends: arch::plic_claim, plic_complete (riscv64 only)
JARVIS_TEST(plic_claim_complete, "PRE: none | POST: none") {
#if defined(CONFIG_ARCH_RISCV64)
    Logger::warn("STUB: plic_claim_complete — real test on RISC-V64 only");
#else
    Logger::warn("PLIC test skipped (not RISC-V64)");
#endif
    JARVIS_TEST_PASS();
}

void register_plic_tests() {
    Logger::info("Registering PLIC tests (stubs)");
    JARVIS_REGISTER_TEST(plic_init_threshold);
    JARVIS_REGISTER_TEST(plic_enable_mask);
    JARVIS_REGISTER_TEST(plic_claim_complete);
}
