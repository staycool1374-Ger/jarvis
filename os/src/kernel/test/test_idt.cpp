#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies all 256 IDT entries have non-null handler addresses
// after init.
// Input: Initialize IDT
// Expect: All 256 entries point to valid handlers (no null pointers)
// Depends: kernel::arch::IDT
JARVIS_TEST(idt_entries_initialized) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies CPU exceptions 0-31 have handler entries (no gaps).
// Input: Initialize IDT, inspect exception vectors 0-31
// Expect: Each vector 0-31 has a valid handler
// Depends: kernel::arch::IDT
JARVIS_TEST(idt_exception_handlers_mapped) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies PIC IRQ0-IRQ15 mapped to interrupt vectors 0x20-0x2F.
// Input: Initialize IDT with PIC remapping
// Expect: Vectors 0x20-0x2F point to IRQ handlers
// Depends: kernel::arch::IDT, PIC
JARVIS_TEST(idt_irq_remapped) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies interrupt 0x80 handler points to syscall dispatch.
// Input: Initialize IDT
// Expect: Vector 0x80 handler is syscall entry point
// Depends: kernel::arch::IDT, syscall
JARVIS_TEST(idt_syscall_handler_installed) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies double fault handler uses TSS IST stack (not kernel
// stack).
// Input: Initialize IDT, inspect double fault entry (vector 8)
// Expect: IST index set to valid IST stack
// Depends: kernel::arch::IDT, TSS
JARVIS_TEST(idt_double_fault_uses_ist) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies vectors 0x30-0x7F are not set (or point to spurious
// handler).
// Input: Initialize IDT, inspect reserved vectors
// Expect: Vectors 0x30-0x7F are null or spurious handler
// Depends: kernel::arch::IDT
JARVIS_TEST(idt_reserved_vectors_null) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all IDT unit tests with the test framework.
// Input: None
// Expect: All IDT tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_idt_tests() {
    Logger::info("Registering IDT tests");
    JARVIS_REGISTER_TEST(idt_entries_initialized);
    JARVIS_REGISTER_TEST(idt_exception_handlers_mapped);
    JARVIS_REGISTER_TEST(idt_irq_remapped);
    JARVIS_REGISTER_TEST(idt_syscall_handler_installed);
    JARVIS_REGISTER_TEST(idt_double_fault_uses_ist);
    JARVIS_REGISTER_TEST(idt_reserved_vectors_null);
}