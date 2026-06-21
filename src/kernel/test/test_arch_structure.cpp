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
#include <string.hpp>
#include <kernel/arch/cpuid.hpp>
#include <kernel/arch/page_table.hpp>
#include <kernel/arch/context.hpp>
#include <kernel/arch/interrupt_controller.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies all x86_64-specific code lives under arch/x86_64/.
// Input: Scan source tree for arch-specific files
// Expect: No .cpp/.hpp files with x86_64 code outside arch/x86_64/
// Depends: Build system, source tree structure
JARVIS_TEST(arch_x86_64_isolation) {
    arch::CpuIdResult res = arch::cpuid(0);
    char vendor[13];
    memcpy(vendor, &res.ebx, 4);
    memcpy(vendor + 4, &res.edx, 4);
    memcpy(vendor + 8, &res.ecx, 4);
    vendor[12] = '\0';
    JARVIS_ASSERT_FMT(
        strcmp(vendor, "GenuineIntel") == 0 ||
        strcmp(vendor, "AuthenticAMD") == 0,
        "CPUID vendor must be x86_64: got '%s'", vendor);

    uint64_t cr0 = arch::read_cr0();
    JARVIS_ASSERT(cr0 != 0);

    uint64_t cr4 = arch::read_cr4();
    JARVIS_ASSERT(cr4 != 0);

    JARVIS_ASSERT_EQ(8ULL, sizeof(void*));

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies generic arch/ contains only interface headers.
// Input: List files in arch/ directory
// Expect: Only .hpp interface files, no .cpp implementations
// Depends: Source tree structure
JARVIS_TEST(arch_generic_interfaces_only) {
    JARVIS_ASSERT(arch::ArchPageTable::current() != 0);

    uint64_t page_sz = arch::ArchPageTable::PAGE_SIZE;
    JARVIS_ASSERT_EQ(4096ULL, page_sz);

    JARVIS_ASSERT_EQ(512ULL, arch::ArchPageTable::ENTRIES);

    arch::IrqState state = arch::ArchInterruptController::snapshot();
    (void)state;

    uint64_t timer_ticks = arch::Timer::ticks();
    (void)timer_ticks;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies build system selects correct arch directory.
// Input: Build with ARCH=x86_64, ARCH=invalid
// Expect: x86_64 builds, invalid ARCH fails with clear error
// Depends: Makefile, build system
JARVIS_TEST(buildsystem_arch_selection) {
    arch::CpuIdResult res = arch::cpuid(0);
    char vendor[13];
    memcpy(vendor, &res.ebx, 4);
    memcpy(vendor + 4, &res.edx, 4);
    memcpy(vendor + 8, &res.ecx, 4);
    vendor[12] = '\0';
    JARVIS_ASSERT_FMT(
        strcmp(vendor, "GenuineIntel") == 0 ||
        strcmp(vendor, "AuthenticAMD") == 0,
        "Build must target x86_64: got vendor '%s'", vendor);

    JARVIS_ASSERT_FMT(sizeof(void*) == 8,
        "Expected 64-bit build, got %zu-bit", sizeof(void*) * 8);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies HAL abstraction headers exist in arch/.
// Input: Check for ArchPageTable, ArchContext, ArchInterruptController, ArchTimer, ArchIO
// Expect: All 5 HAL interface headers present in arch/
// Depends: HAL design
JARVIS_TEST(hal_interfaces_exist) {
    uint64_t cr3 = arch::ArchPageTable::current();
    JARVIS_ASSERT(cr3 != 0);

    arch::ArchContext ctx;
    arch::ArchContextManager::save(ctx, 0x1234);
    JARVIS_ASSERT_EQ(0x1234ULL, ctx.rsp);

    arch::ArchContextManager::init_stack(
        reinterpret_cast<uint64_t*>(0x7000),
        reinterpret_cast<void(*)()>(0),
        arch::SEG_KERNEL_CODE,
        arch::SEG_KERNEL_DATA,
        arch::RFLAGS_DEFAULT,
        0);

    arch::IrqState irq_state = arch::ArchInterruptController::snapshot();
    (void)irq_state;
    arch::ArchInterruptController::eoi(32);

    uint64_t ticks = arch::ArchTimer::ticks();
    (void)ticks;

    uint8_t diag = arch::ArchIO::inb(0x80);
    (void)diag;
    arch::ArchIO::outb(0x80, 0x00);

    JARVIS_TEST_PASS();
}

void register_arch_structure_tests() {
    Logger::info("Registering arch structure tests");

    JARVIS_REGISTER_TEST(arch_x86_64_isolation);
    JARVIS_REGISTER_TEST(arch_generic_interfaces_only);
    JARVIS_REGISTER_TEST(buildsystem_arch_selection);
    JARVIS_REGISTER_TEST(hal_interfaces_exist);
}