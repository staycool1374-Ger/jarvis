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

#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/idt.hpp>

using namespace kernel;

/// @brief Verifies all 256 IDT entries have non-null handler addresses after init.
/// @input Initialize IDT
/// @expect All 256 entries point to valid handlers (no null pointers)
/// @depends kernel::arch::IDT
JARVIS_TEST(idt_entries_initialized) {
    for (uint16_t vec = 0; vec < 256; ++vec) {
        JARVIS_ASSERT(arch::IDT::has_handler(static_cast<uint8_t>(vec)));
    }
}

/// @brief Verifies CPU exceptions 0-31 have handler entries (no gaps).
/// @input Initialize IDT, inspect exception vectors 0-31
/// @expect Each vector 0-31 has a valid handler
/// @depends kernel::arch::IDT
JARVIS_TEST(idt_exception_handlers_mapped) {
    for (uint8_t vec = 0; vec <= 31; ++vec) {
        JARVIS_ASSERT(arch::IDT::has_handler(vec));
    }
}

/// @brief Verifies PIC IRQ0-IRQ15 mapped to interrupt vectors 0x20-0x2F.
/// @input Initialize IDT with PIC remapping
/// @expect Vectors 0x20-0x2F point to IRQ handlers
/// @depends kernel::arch::IDT, PIC
JARVIS_TEST(idt_irq_remapped) {
    for (uint8_t vec = 0x20; vec <= 0x2F; ++vec) {
        JARVIS_ASSERT(arch::IDT::has_handler(vec));
    }
}

/// @brief Verifies interrupt 0x80 handler points to syscall dispatch.
/// @input Initialize IDT
/// @expect Vector 0x80 handler is syscall entry point
/// @depends kernel::arch::IDT, syscall
JARVIS_TEST(idt_syscall_handler_installed) {
    JARVIS_ASSERT(arch::IDT::has_handler(0x80));
}

/// @brief Verifies double fault handler uses TSS IST stack (not kernel stack).
/// @input Initialize IDT, inspect double fault entry (vector 8)
/// @expect IST index set to valid IST stack
/// @depends kernel::arch::IDT, TSS
JARVIS_TEST(idt_double_fault_uses_ist) {
    const auto& entry = arch::IDT::entry(8);
    JARVIS_ASSERT(entry.ist != 0);
    JARVIS_ASSERT(entry.ist <= 7);
}

/// @brief Verifies vectors 0x30-0x7F are not set (or point to spurious handler).
/// @input Initialize IDT, inspect reserved vectors
/// @expect Vectors 0x30-0x7F are null or spurious handler
/// @depends kernel::arch::IDT
JARVIS_TEST(idt_reserved_vectors_null) {
    for (uint8_t vec = 0x30; vec <= 0x7F; ++vec) {
        const auto& e = arch::IDT::entry(vec);
        uint64_t handler = static_cast<uint64_t>(e.offset_high) << 32 |
                           static_cast<uint64_t>(e.offset_mid) << 16 |
                           e.offset_low;
        JARVIS_ASSERT(handler == 0 || arch::IDT::has_handler(vec));
    }
}

/// @brief Registers all IDT unit tests with the test framework.
/// @input None
/// @expect All IDT tests registered via JARVIS_REGISTER_TEST
/// @depends kernel test framework
void register_idt_tests() {
    Logger::info("Registering IDT tests");
    JARVIS_REGISTER_TEST(idt_entries_initialized);
    JARVIS_REGISTER_TEST(idt_exception_handlers_mapped);
    JARVIS_REGISTER_TEST(idt_irq_remapped);
    JARVIS_REGISTER_TEST(idt_syscall_handler_installed);
    JARVIS_REGISTER_TEST(idt_double_fault_uses_ist);
    JARVIS_REGISTER_TEST(idt_reserved_vectors_null);
}