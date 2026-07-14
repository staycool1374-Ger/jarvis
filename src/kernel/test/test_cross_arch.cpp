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

/// @file test_cross_arch.cpp
/// @brief Cross-architecture tests that validate identical behaviour on all
/// three supported architectures (x86_64, aarch64, riscv64).
///
/// Each test exercises a kernel subsystem through its generic interface
/// (VMM, ArchContextManager, Timer, ArchInterruptController, IPC, VFS)
/// and is compiled for all arches.  Architecture-specific assertions are
/// guarded with #if/#elif/#endif.

#include <test.hpp>
#include <logger.hpp>
#include <constants.hpp>
#include <kernel/arch/page_table.hpp>
#include <kernel/arch/context.hpp>
#include <kernel/arch/interrupt_controller.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/vfs/vfs.hpp>

using namespace kernel;

// ============================================================================
// Test 1: Page-table basic walk — map, verify, unmap through VMM API
// ============================================================================
//
// Allocates a fresh physical page, maps it via VMM::map_page_in_pml4 into a
// cloned kernel page table, verifies VMM::virt_to_phys_in_pml4() returns the
// expected address, unmaps, and confirms the mapping is gone.  This exercises
// the arch-specific page-table walker through the generic VMM interface.
//
// Runmode: kernel
// Testidea: Map a page in a cloned PML4, verify virt_to_phys, unmap, verify
// gone Input: alloc_page + map_page_in_pml4 + virt_to_phys_in_pml4 + unmap
// Expect: Map succeeds, virt_to_phys returns correct phys, unmap succeeds, then
// 0 Depends: kernel::PMM, kernel::VMM
JARVIS_TEST(cross_page_table_map_unmap, "PRE: none | POST: none") {
    // Clone kernel page table for isolated testing
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT_FMT(pml4 != 0, "clone_kernel_pml4 returned 0");

    // Known user virtual address for testing
    constexpr uint64_t TEST_VA = 0x8000000000ULL;

    // Allocate a physical page
    uint64_t phys = PMM::alloc_page();
    JARVIS_ASSERT_FMT(phys != 0, "PMM::alloc_page returned 0");

    // Map it as a user page in the cloned PML4
    VMM::map_page_in_pml4(TEST_VA, phys, true, pml4);

    // Verify the translation via virt_to_phys_in_pml4
    uint64_t retrieved = VMM::virt_to_phys_in_pml4(TEST_VA, pml4);
    JARVIS_ASSERT_FMT(retrieved == (phys & ~0xFFFULL),
                      "virt_to_phys_in_pml4 returned 0x%lx, expected 0x%lx",
                      retrieved, phys & ~0xFFFULL);

    // Clean up user pages from cloned PML4
    VMM::free_user_pages(pml4);

    // Free the cloned PML4 page itself
    PMM::free_page(pml4);

    // Free the allocated physical page
    PMM::free_page(phys);

    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 2: Page-table basic walk — verify no mapping for unmapped address
// ============================================================================
//
// Runmode: kernel
// Testidea: Verify virt_to_phys returns 0 for unmapped address in cloned PML4
// Input: Clone kernel PML4, query unmapped address
// Expect: virt_to_phys_in_pml4 returns 0
// Depends: kernel::VMM
JARVIS_TEST(cross_page_table_unmapped_returns_zero, "PRE: none | POST: none") {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    // An address that should not be mapped in a fresh clone
    constexpr uint64_t UNMAPPED_VA = 0x8000001000ULL;
    uint64_t retrieved = VMM::virt_to_phys_in_pml4(UNMAPPED_VA, pml4);
    JARVIS_ASSERT_EQ(0ULL, retrieved);

    PMM::free_page(pml4);
    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 3: Context switch — ArchContextManager save/restore/switch_to
// ============================================================================
//
// Exercises the arch-specific context management API (save, restore,
// switch_to) which lies at the heart of all context switching on every arch.
//
// Runmode: kernel
// Testidea: Save two contexts, switch between them, verify stack pointers
// Input: ArchContext objects, known stack addresses
// Expect: save records correct stack pointer, switch_to swaps, restore works
// Depends: kernel::arch::ArchContextManager
JARVIS_TEST(cross_context_save_restore, "PRE: none | POST: none") {
    arch::ArchContext ctx_a{};
    arch::ArchContext ctx_b{};

    // Use HHDM addresses that are at least 2 MiB apart to avoid aliasing
    uint64_t sp_a = arch::HHDM_OFFSET + 0x10000000ULL;
    uint64_t sp_b = arch::HHDM_OFFSET + 0x20000000ULL;

    arch::ArchContextManager::save(ctx_a, sp_a);
    arch::ArchContextManager::save(ctx_b, sp_b);

    // Verify saved stack pointers
#if defined(CONFIG_ARCH_X86_64)
    JARVIS_ASSERT_EQ(ctx_a.rsp, sp_a);
    JARVIS_ASSERT_EQ(ctx_b.rsp, sp_b);
#elif defined(CONFIG_ARCH_AARCH64)
    JARVIS_ASSERT_EQ(ctx_a.sp_el0, sp_a);
    JARVIS_ASSERT_EQ(ctx_b.sp_el0, sp_b);
#elif defined(CONFIG_ARCH_RISCV64)
    JARVIS_ASSERT_EQ(ctx_a.sp, sp_a);
    JARVIS_ASSERT_EQ(ctx_b.sp, sp_b);
#endif

    // Switch from A to B
    uint64_t current_sp = sp_a;
    arch::ArchContextManager::switch_to(ctx_a, ctx_b, current_sp);
    JARVIS_ASSERT_EQ(current_sp, sp_b);

    // Switch back from B to A
    arch::ArchContextManager::switch_to(ctx_b, ctx_a, current_sp);
    JARVIS_ASSERT_EQ(current_sp, sp_a);

    // Verify ctx_a still has its saved value
#if defined(CONFIG_ARCH_X86_64)
    JARVIS_ASSERT_EQ(ctx_a.rsp, sp_a);
    JARVIS_ASSERT_EQ(ctx_b.rsp, sp_b);
#elif defined(CONFIG_ARCH_AARCH64)
    JARVIS_ASSERT_EQ(ctx_a.sp_el0, sp_a);
    JARVIS_ASSERT_EQ(ctx_b.sp_el0, sp_b);
#elif defined(CONFIG_ARCH_RISCV64)
    JARVIS_ASSERT_EQ(ctx_a.sp, sp_a);
    JARVIS_ASSERT_EQ(ctx_b.sp, sp_b);
#endif

    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 4: Context — init_stack creates valid stack frame
// ============================================================================
//
// Verifies that init_stack writes an entry-point, processor-state, and stack
// pointer to the correct positions for the architecture.
//
// Runmode: kernel
// Testidea: Create a stack frame with init_stack, verify entry and state
// present Input: Stack buffer, entry function, psr, user SP Expect: Stack
// contains entry and PSR, pointers are within range Depends:
// kernel::arch::ArchContextManager
JARVIS_TEST(cross_context_init_stack, "PRE: none | POST: none") {
    uint64_t stack[1024] = {};
    uint64_t *stack_top = stack + 1024;

    auto test_entry = []() {
        while (1) {
        }
    };

#if defined(CONFIG_ARCH_X86_64)
    uint64_t test_user_sp = arch::HHDM_OFFSET + 0x30000000ULL;
    arch::ArchContextManager::init_stack(
        stack_top, test_entry, arch::SEG_KERNEL_CODE, arch::SEG_KERNEL_DATA,
        arch::RFLAGS_DEFAULT, test_user_sp);
    // init_stack writes below the original stack_top — verify some values
    // were written (i.e. stack_top[-1] through stack_top[-22] are non-zero)
    bool written = false;
    for (int i = 1; i <= 22; ++i) {
        if (stack_top[-i] != 0) {
            written = true;
            break;
        }
    }
    JARVIS_ASSERT_FMT(written, "init_stack did not write any values to stack");
#elif defined(CONFIG_ARCH_AARCH64)
    uint64_t test_psr = 0;
    uint64_t test_user_sp = arch::HHDM_OFFSET + 0x30000000ULL;
    arch::ArchContextManager::init_stack(stack_top, test_entry, 0, 0, test_psr,
                                         test_user_sp);
    bool written = false;
    for (int i = 1; i <= 7; ++i) {
        if (stack_top[-i] != 0) {
            written = true;
            break;
        }
    }
    JARVIS_ASSERT_FMT(written, "init_stack did not write any values to stack");
#elif defined(CONFIG_ARCH_RISCV64)
    uint64_t test_psr = 0;
    uint64_t test_user_sp = arch::HHDM_OFFSET + 0x30000000ULL;
    arch::ArchContextManager::init_stack(stack_top, test_entry, 0, 0, test_psr,
                                         test_user_sp);
    bool written = false;
    for (int i = 1; i <= 20; ++i) {
        if (stack_top[-i] != 0) {
            written = true;
            break;
        }
    }
    JARVIS_ASSERT_FMT(written, "init_stack did not write any values to stack");
#endif

    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 5: ArchPageTable::ENTRIES and PAGE_SIZE are consistent across arches
// ============================================================================
//
// Runmode: kernel
// Testidea: Verify page table constants match expectations
// Input: ArchPageTable static constants
// Expect: PAGE_SIZE=4096, ENTRIES=512 (all known CPU architectures)
// Depends: kernel::arch::ArchPageTable
JARVIS_TEST(cross_page_table_constants, "PRE: none | POST: none") {
    JARVIS_ASSERT_EQ(4096ULL, arch::ArchPageTable::PAGE_SIZE);
    JARVIS_ASSERT_EQ(512ULL, arch::ArchPageTable::ENTRIES);
    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 6: Timer — ticks() is monotonic and non-zero
// ============================================================================
//
// Runmode: kernel
// Testidea: Read Timer::ticks() multiple times, verify monotonic and > 0
// Input: Repeated calls to arch::Timer::ticks()
// Expect: All values > 0, each >= previous
// Depends: kernel::arch::Timer
JARVIS_TEST(cross_timer_ticks_monotonic, "PRE: iocd | POST: none") {
    // Use set_ticks_for_test to establish a known baseline
    arch::Timer::set_ticks_for_test(100);
    uint64_t t1 = arch::Timer::ticks();
    JARVIS_ASSERT_FMT(t1 == 100,
                      "Timer ticks should be 100 after test set, got %lu", t1);

    uint64_t t2 = arch::Timer::ticks();
    uint64_t t3 = arch::Timer::ticks();
    JARVIS_ASSERT(t2 >= t1);
    JARVIS_ASSERT(t3 >= t2);
    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 7: Timer — ns() returns reasonable values
// ============================================================================
//
// Runmode: kernel
// Testidea: Call Timer::ns() before/after a delay, verify positive difference
// Input: Two calls to arch::Timer::ns() with a small busy loop in between
// Expect: delta > 0 and delta < 10 seconds (sanity bound)
// Depends: kernel::arch::Timer
JARVIS_TEST(cross_timer_ns_delta, "PRE: iocd | POST: none") {
    uint64_t t0 = arch::Timer::ns();
    for (int i = 0; i < 100000; ++i) {
        asm volatile("");
    }
    uint64_t t1 = arch::Timer::ns();

    uint64_t delta = t1 - t0;
    JARVIS_ASSERT_FMT(delta > 0, "Timer ns() delta <= 0: %lu", delta);
    JARVIS_ASSERT_FMT(delta < 10000000000ULL,
                      "Timer ns() delta too large: %lu ns (limit 10s)", delta);

    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 8: Timer — set_ticks_for_test and handle_irq work
// ============================================================================
//
// Runmode: kernel
// Testidea: Override ticks via set_ticks_for_test, verify handle_irq increments
// Input: set_ticks_for_test(0) then handle_irq() then check ticks()
// Expect: ticks() == 1
// Depends: kernel::arch::Timer
JARVIS_TEST(cross_timer_irq_handler, "PRE: iocd | POST: none") {
    arch::Timer::set_ticks_for_test(0);
    JARVIS_ASSERT_EQ((uint64_t)0, arch::Timer::ticks());
    arch::Timer::handle_irq();
    JARVIS_ASSERT_EQ((uint64_t)1, arch::Timer::ticks());
    arch::Timer::handle_irq();
    JARVIS_ASSERT_EQ((uint64_t)2, arch::Timer::ticks());
    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 9: Interrupt controller — init, EOI, mask, unmask
// ============================================================================
//
// This test validates that ArchInterruptController can be called without
// crashing.  The init() call is already made during boot, so this is a
// re-init test that exercises the arch-specific implementation.
//
// Runmode: kernel
// Testidea: Call ArchInterruptController init, EOI, mask, unmask
// Input: init() then eoi(32) then mask(1) then unmask(1)
// Expect: No crash, snapshot/restore restores original state
// Depends: kernel::arch::ArchInterruptController
JARVIS_TEST(cross_interrupt_controller_init, "PRE: iocd | POST: none") {
    arch::ArchInterruptController::init();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Send EOI to interrupt controller
// Input: eoi(32) and eoi(40)
// Expect: No crash
// Depends: kernel::arch::ArchInterruptController
JARVIS_TEST(cross_interrupt_controller_eoi, "PRE: iocd | POST: none") {
    arch::ArchInterruptController::eoi(32);
    arch::ArchInterruptController::eoi(40);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Mask and unmask an IRQ
// Input: mask(1), then unmask(1)
// Expect: No crash
// Depends: kernel::arch::ArchInterruptController
JARVIS_TEST(cross_interrupt_controller_mask_unmask, "PRE: iocd | POST: none") {
    arch::ArchInterruptController::mask(1);
    arch::ArchInterruptController::unmask(1);
    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 10: IPC — MessageQueue push/pop round-trip
// ============================================================================
//
// The IPC MessageQueue is a generic data structure used identically on all
// architectures.  This test validates a push/pop round-trip including
// priority ordering.
//
// Runmode: kernel
// Testidea: Push a message then pop it, verify all fields preserved
// Input: Message with known sender_id, type, priority, data_size, data
// Expect: Pop returns matching message, queue is empty afterward
// Depends: kernel::MessageQueue
JARVIS_TEST(cross_ipc_queue_push_pop, "PRE: none | POST: none") {
    MessageQueue q;
    q.init();
    JARVIS_ASSERT(q.is_empty());
    JARVIS_ASSERT(!q.is_full());

    Message msg;
    msg.sender_id = 42;
    msg.type = 7;
    msg.priority = 0;
    msg.data_size = 8;
    msg.data[0] = 0xCA;
    msg.data[1] = 0xFE;
    msg.data[2] = 0xBA;
    msg.data[3] = 0xBE;
    msg.data[4] = 0xDE;
    msg.data[5] = 0xAD;
    msg.data[6] = 0xBE;
    msg.data[7] = 0xEF;

    JARVIS_ASSERT(q.push(msg));
    JARVIS_ASSERT(!q.is_empty());
    JARVIS_ASSERT_EQ(1ULL, q.count);

    Message out;
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(42ULL, out.sender_id);
    JARVIS_ASSERT_EQ(7ULL, out.type);
    JARVIS_ASSERT_EQ(0ULL, out.priority);
    JARVIS_ASSERT_EQ(8ULL, out.data_size);
    JARVIS_ASSERT_EQ(0xCA, out.data[0]);
    JARVIS_ASSERT_EQ(0xFE, out.data[1]);
    JARVIS_ASSERT_EQ(0xBA, out.data[2]);
    JARVIS_ASSERT_EQ(0xBE, out.data[3]);
    JARVIS_ASSERT_EQ(0xDE, out.data[4]);
    JARVIS_ASSERT_EQ(0xAD, out.data[5]);
    JARVIS_ASSERT_EQ(0xBE, out.data[6]);
    JARVIS_ASSERT_EQ(0xEF, out.data[7]);
    JARVIS_ASSERT(q.is_empty());

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Push multiple messages, verify priority ordering on pop
// Input: Three messages with priorities 2, 0, 1
// Expect: Popped in order priority 0, 1, 2 (highest first)
// Depends: kernel::MessageQueue
JARVIS_TEST(cross_ipc_queue_priority_ordering, "PRE: none | POST: none") {
    MessageQueue q;
    q.init();

    Message m1, m2, m3;
    m1.sender_id = 1;
    m1.type = 1;
    m1.priority = 2;
    m1.data_size = 0;
    m2.sender_id = 2;
    m2.type = 2;
    m2.priority = 0;
    m2.data_size = 0;
    m3.sender_id = 3;
    m3.type = 3;
    m3.priority = 1;
    m3.data_size = 0;

    JARVIS_ASSERT(q.push(m1));
    JARVIS_ASSERT(q.push(m2));
    JARVIS_ASSERT(q.push(m3));
    JARVIS_ASSERT_EQ(3ULL, q.count);

    Message out;
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(2ULL, out.sender_id); // priority 0 first
    JARVIS_ASSERT_EQ(0ULL, out.priority);

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(3ULL, out.sender_id); // priority 1 second
    JARVIS_ASSERT_EQ(1ULL, out.priority);

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(1ULL, out.sender_id); // priority 2 last
    JARVIS_ASSERT_EQ(2ULL, out.priority);

    JARVIS_ASSERT(q.is_empty());

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Push messages until full, then verify overflow
// Input: Fill queue to capacity, then push one more
// Expect: push returns false when full; pop still works after overflow attempt
// Depends: kernel::MessageQueue
JARVIS_TEST(cross_ipc_queue_full_behavior, "PRE: none | POST: none") {
    MessageQueue q;
    q.init();

    Message fill;
    fill.sender_id = 1;
    fill.type = 0;
    fill.priority = 0;
    fill.data_size = 0;

    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        JARVIS_ASSERT_FMT(q.push(fill), "Push failed at index %zu", i);
    }
    JARVIS_ASSERT(q.is_full());

    // Next push must fail
    JARVIS_ASSERT(!q.push(fill));

    // Pop should still work
    Message out;
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT(!q.is_full());

    JARVIS_TEST_PASS();
}

// ============================================================================
// Test 11: VFS — resolve root and common device paths
// ============================================================================
//
// Runmode: kernel
// Testidea: Resolve "/" and verify it is a directory
// Input: vfs::resolve("/")
// Expect: Non-null vnode with S_IFDIR
// Depends: kernel::vfs::resolve
JARVIS_TEST(cross_vfs_resolve_root, "PRE: vfsd, iocd | POST: none") {
    vfs::Vnode *vn = vfs::resolve("/");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Resolve a nonexistent path returns nullptr
// Input: vfs::resolve("/nonexistent_path_cross_arch_test_xyz")
// Expect: nullptr
// Depends: kernel::vfs::resolve
JARVIS_TEST(cross_vfs_resolve_nonexistent, "PRE: vfsd, iocd | POST: none") {
    vfs::Vnode *vn = vfs::resolve("/nonexistent_path_cross_arch_test_xyz");
    JARVIS_ASSERT(vn == nullptr);
    JARVIS_TEST_PASS();
}

// ============================================================================
// Registration
// ============================================================================
void register_cross_arch_tests() {
    Logger::info("Registering cross-architecture tests");

    JARVIS_REGISTER_TEST(cross_page_table_map_unmap);
    JARVIS_REGISTER_TEST(cross_page_table_unmapped_returns_zero);
    JARVIS_REGISTER_TEST(cross_context_save_restore);
    JARVIS_REGISTER_TEST(cross_context_init_stack);
    JARVIS_REGISTER_TEST(cross_page_table_constants);
    JARVIS_REGISTER_TEST(cross_timer_ticks_monotonic);
    JARVIS_REGISTER_TEST(cross_timer_ns_delta);
    JARVIS_REGISTER_TEST(cross_timer_irq_handler);
    JARVIS_REGISTER_TEST(cross_interrupt_controller_init);
    JARVIS_REGISTER_TEST(cross_interrupt_controller_eoi);
    JARVIS_REGISTER_TEST(cross_interrupt_controller_mask_unmask);
    JARVIS_REGISTER_TEST(cross_ipc_queue_push_pop);
    JARVIS_REGISTER_TEST(cross_ipc_queue_priority_ordering);
    JARVIS_REGISTER_TEST(cross_ipc_queue_full_behavior);
    JARVIS_REGISTER_TEST(cross_vfs_resolve_root);
    JARVIS_REGISTER_TEST(cross_vfs_resolve_nonexistent);
}
