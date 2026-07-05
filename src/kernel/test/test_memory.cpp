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
#include <constants.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Allocate and free a single physical page, verifying the free
// memory count is updated correctly.
// Input: Single page alloc_page() then free_page().
// Expect: Alloc returns non-zero, free_memory decreases by 4096 then returns
// to original after free.
// Depends: PMM
JARVIS_TEST(pmm_alloc_free, "PRE: none | POST: none") {
    uint64_t before = PMM::free_memory();
    uint64_t p1 = PMM::alloc_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(PMM::free_memory() == before - 4096);
    PMM::free_page(p1);
    JARVIS_ASSERT(PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Allocate and free 4 contiguous physical pages, verifying the
// free memory count drops accordingly.
// Input: alloc_contiguous(4) then free each page individually.
// Expect: Non-zero base address, free_memory decreases by at least 4*4096,
// all pages freed cleanly.
// Depends: PMM
JARVIS_TEST(pmm_alloc_contiguous, "PRE: none | POST: none") {
    uint64_t before = PMM::free_memory();
    uint64_t pages = PMM::alloc_contiguous(4);
    JARVIS_ASSERT(pages != 0);
    JARVIS_ASSERT(PMM::free_memory() <= before - 4 * 4096);
    for (size_t i = 0; i < 4; ++i)
        PMM::free_page(pages + i * 4096);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Allocate a user page and verify it is correctly marked as a user
// page.
// Input: alloc_user_page() then free_page().
// Expect: Non-zero page returned, is_user_page returns true, free memory
// restored after free.
// Depends: PMM
JARVIS_TEST(pmm_user_alloc, "PRE: none | POST: none") {
    uint64_t before = PMM::free_memory();
    uint64_t p1 = PMM::alloc_user_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(PMM::is_user_page(p1));
    PMM::free_page(p1);
    JARVIS_ASSERT(PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify total_memory returns a positive value at least as large
// as free_memory.
// Input: None (reads global PMM state).
// Expect: total_memory > 0 and total_memory >= free_memory.
// Depends: PMM
JARVIS_TEST(pmm_total_memory, "PRE: none | POST: none") {
    uint64_t total = PMM::total_memory();
    JARVIS_ASSERT(total > 0);
    JARVIS_ASSERT(total >= PMM::free_memory());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify free_user_pages does not crash when the PML4 contains
// kernel-owned entries mixed with user pages.
// Input: Cloned kernel PML4 with one user page mapped at 0x8000000000.
// Expect: free_user_pages completes without error.
// Depends: PMM, VMM
JARVIS_TEST(vmm_free_user_pages_skips_kernel_owned_entries, "PRE: none | POST: none") {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    uint64_t user_page = PMM::alloc_user_page();
    JARVIS_ASSERT(user_page != 0);
    VMM::map_page_in_pml4(0x8000000000ULL, user_page, true, pml4);

    VMM::free_user_pages(pml4);

    PMM::free_page(pml4);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify free_user_pages handles a user stack VADDR mapping
// (simulating fork cleanup).
// Input: Cloned kernel PML4 with a user page mapped at mem::STACK_VADDR.
// Expect: free_user_pages completes cleanly without errors.
// Depends: PMM, VMM, mem
JARVIS_TEST(vmm_free_user_pages_fork_stack_scenario, "PRE: none | POST: none") {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    uint64_t stack_page = PMM::alloc_user_page();
    JARVIS_ASSERT(stack_page != 0);
    VMM::map_page_in_pml4(mem::STACK_VADDR, stack_page, true, pml4);

    VMM::free_user_pages(pml4);

    PMM::free_page(pml4);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Allocate two blocks of different sizes from MemPool and verify
// they return valid distinct pointers.
// Input: alloc(16) then alloc(32).
// Expect: Both non-null, pointers differ, free succeeds for both.
// Depends: MemPool
JARVIS_TEST(mempool_alloc_free, "PRE: none | POST: none") {
    void* p1 = MemPool::alloc(16);
    JARVIS_ASSERT(p1 != nullptr);
    void* p2 = MemPool::alloc(32);
    JARVIS_ASSERT(p2 != nullptr);
    JARVIS_ASSERT(p2 != p1);
    MemPool::free(p1);
    MemPool::free(p2);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Allocate a large (1024-byte) block from MemPool.
// Input: alloc(1024).
// Expect: Non-null pointer returned, free succeeds.
// Depends: MemPool
JARVIS_TEST(mempool_large_alloc, "PRE: none | POST: none") {
    void* p = MemPool::alloc(1024);
    JARVIS_ASSERT(p != nullptr);
    MemPool::free(p);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify MemPool handles fragmentation by allocating and freeing
// 10 blocks then re-allocating.
// Input: 10 sequential alloc(64) then free all, then alloc(64) again.
// Expect: All allocations return non-null, final re-alloc succeeds.
// Depends: MemPool
JARVIS_TEST(mempool_fragmentation, "PRE: none | POST: none") {
    void* ptrs[10];
    for (int i = 0; i < 10; ++i) {
        ptrs[i] = MemPool::alloc(64);
        JARVIS_ASSERT(ptrs[i] != nullptr);
    }
    for (int i = 0; i < 10; ++i)
        MemPool::free(ptrs[i]);
    void* p = MemPool::alloc(64);
    JARVIS_ASSERT(p != nullptr);
    MemPool::free(p);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify MemPool reuses a freed block when the same size is
// requested again.
// Input: alloc(16), free, then alloc(16) again.
// Expect: Second alloc returns the same address as the first.
// Depends: MemPool
JARVIS_TEST(mempool_reuse, "PRE: none | POST: none") {
    void* p1 = MemPool::alloc(16);
    JARVIS_ASSERT(p1 != nullptr);
    MemPool::free(p1);
    void* p2 = MemPool::alloc(16);
    JARVIS_ASSERT(p2 != nullptr);
    JARVIS_ASSERT(p1 == p2);
    MemPool::free(p2);
    JARVIS_TEST_PASS();
}

// ──────────────────────────────────────────────
// VMM regression tests for the three bugs fixed
// in commit d7aee03.
// ──────────────────────────────────────────────

// Runmode: kernel
// Testidea: Regression test for bug #3 — verify map_page splits a 2 MiB huge
// page in the kernel HHDM and correctly maps a 4 KiB target page while
// preserving neighbouring translations.
// Input: Virtual address at HHDM_OFFSET+0x802000 inside a 2 MiB huge page;
// allocate a different physical page and map it via map_page.
// Expect: After map, target resolves to new physical page; neighbouring
// pages still resolve correctly via the newly allocated page table; original
// mapping is restored.
// Depends: PMM, VMM, arch
#if defined(CONFIG_ARCH_X86_64)
JARVIS_TEST(vmm_huge_page_split_regression, "PRE: none | POST: none") {
    constexpr uint64_t test_vaddr = arch::HHDM_OFFSET + 0x802000;
    constexpr uint64_t huge_page_base = arch::HHDM_OFFSET + 0x800000;
    uint64_t const PAGE_PRESENT = 1ULL << 0;
    uint64_t const PAGE_HUGE    = 1ULL << 7;

    uint64_t kernel_pml4 = arch::read_cr3();
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (kernel_pml4 & ~0xFFFULL));
    size_t pml4_idx = static_cast<size_t>((test_vaddr & (0x1FFULL << 39)) >> 39);
    JARVIS_ASSERT(pml4[pml4_idx] & PAGE_PRESENT);
    auto* pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4[pml4_idx] & ~0xFFFULL));
    size_t pdpt_idx = static_cast<size_t>((test_vaddr & (0x1FFULL << 30)) >> 30);
    JARVIS_ASSERT(pdpt[pdpt_idx] & PAGE_PRESENT);
    auto* pd = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pdpt[pdpt_idx] & ~0xFFFULL));
    size_t pd_idx = static_cast<size_t>((test_vaddr & (0x1FFULL << 21)) >> 21);
    JARVIS_ASSERT(pd[pd_idx] & PAGE_PRESENT);
    JARVIS_ASSERT(pd[pd_idx] & PAGE_HUGE);
    uint64_t const saved_pd_entry = pd[pd_idx];

    // Before the split the huge page gives us identity-like translation.
    uint64_t before = VMM::virt_to_phys(test_vaddr);
    JARVIS_ASSERT(before == 0x802000); // identity within the 2 MiB huge page

    uint64_t before_base = VMM::virt_to_phys(huge_page_base);
    JARVIS_ASSERT(before_base == 0x800000);

    uint64_t before_neighbour = VMM::virt_to_phys(huge_page_base + 0x1000);
    JARVIS_ASSERT(before_neighbour == 0x801000);

    // Allocate a fresh physical page and map it – this triggers the huge-page
    // split inside map_page.
    uint64_t test_phys = PMM::alloc_page();
    JARVIS_ASSERT(test_phys != 0);
    JARVIS_ASSERT(test_phys != 0x802000); // must differ from original

    VMM::map_page(test_vaddr, test_phys, false);

    // After the split the target address should resolve to the new page.
    uint64_t after = VMM::virt_to_phys(test_vaddr);
    JARVIS_ASSERT(after == (test_phys & ~0xFFFULL));

    // Neighbouring pages inside the original huge page must still resolve
    // correctly via the newly allocated page table.
    uint64_t after_base = VMM::virt_to_phys(huge_page_base);
    JARVIS_ASSERT(after_base == 0x800000);

    uint64_t after_neighbour = VMM::virt_to_phys(huge_page_base + 0x1000);
    JARVIS_ASSERT(after_neighbour == 0x801000);

    // Restore the original mapping.
    VMM::unmap_page(test_vaddr);
    VMM::map_page(test_vaddr, 0x802000, false);
    JARVIS_ASSERT(VMM::virt_to_phys(test_vaddr) == 0x802000);

    // Undo the huge-page split: free the page-table page and restore the PD entry.
    uint64_t pt_phys = pd[pd_idx] & ~0xFFFULL;
    JARVIS_ASSERT(!(pd[pd_idx] & PAGE_HUGE)); // must point to a PT now (no huge flag)
    PMM::free_page(pt_phys);
    pd[pd_idx] = saved_pd_entry;

    PMM::free_page(test_phys);
    JARVIS_TEST_PASS();
}
#endif

#if defined(CONFIG_ARCH_X86_64)
JARVIS_TEST(vmm_hhdm_access_consistency, "PRE: none | POST: none") {
    uint64_t kernel_pml4 = VMM::get_kernel_pml4();
    JARVIS_ASSERT(kernel_pml4 != 0);

    uint64_t kpml4_virt = VMM::virt_to_phys(reinterpret_cast<uint64_t>(&VMM::init));
    JARVIS_ASSERT(kpml4_virt != 0);

    uint64_t v = arch::HHDM_OFFSET + 0x900000;

    uint64_t cr3 = arch::read_cr3();
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (cr3 & ~0xFFFULL));
    size_t pml4_i = static_cast<size_t>((v & (0x1FFULL << 39)) >> 39);
    auto* pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4[pml4_i] & ~0xFFFULL));
    size_t pdpt_i = static_cast<size_t>((v & (0x1FFULL << 30)) >> 30);
    auto* pd = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pdpt[pdpt_i] & ~0xFFFULL));
    size_t pd_i = static_cast<size_t>((v & (0x1FFULL << 21)) >> 21);
    JARVIS_ASSERT(pd[pd_i] & (1ULL << 7));
    uint64_t const saved_pd_entry = pd[pd_i];

    uint64_t p = PMM::alloc_page();
    JARVIS_ASSERT(p != 0);
    VMM::map_page(v, p, false);
    JARVIS_ASSERT(VMM::virt_to_phys(v) == (p & ~0xFFFULL));
    VMM::unmap_page(v);
    JARVIS_ASSERT(VMM::virt_to_phys(v) == 0);
    VMM::map_page(v, 0x900000, false);

    uint64_t pt_phys = pd[pd_i] & ~0xFFFULL;
    PMM::free_page(pt_phys);
    pd[pd_i] = saved_pd_entry;

    PMM::free_page(p);
    JARVIS_TEST_PASS();
}
#endif

// Runmode: kernel
// Testidea: Verify alloc_page_table returns a page below 128 MiB so the HHDM
// huge-page mapping can reach it.
// Input: alloc_page_table().
// Expect: Non-zero page address below 128 MiB.
// Depends: PMM
JARVIS_TEST(pmm_alloc_page_table, "PRE: none | POST: none") {
    uint64_t pt = PMM::alloc_page_table();
    JARVIS_ASSERT(pt != 0);
#if defined(CONFIG_ARCH_X86_64)
    JARVIS_ASSERT(pt < 128ULL * 1024 * 1024);
#endif
    PMM::free_page(pt);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Register all memory subsystem tests (PMM, VMM, MemPool) with the
// test framework.
// Depends: PMM, VMM, MemPool
void register_memory_tests() {
    Logger::info("Registering memory tests");

    JARVIS_REGISTER_TEST(pmm_alloc_free);
    JARVIS_REGISTER_TEST(pmm_alloc_contiguous);
    JARVIS_REGISTER_TEST(pmm_user_alloc);
    JARVIS_REGISTER_TEST(pmm_total_memory);
    JARVIS_REGISTER_TEST(pmm_alloc_page_table);
#if defined(CONFIG_ARCH_X86_64)
    JARVIS_REGISTER_TEST(vmm_huge_page_split_regression);
    JARVIS_REGISTER_TEST(vmm_hhdm_access_consistency);
#endif
    JARVIS_REGISTER_TEST(vmm_free_user_pages_skips_kernel_owned_entries);
    JARVIS_REGISTER_TEST(vmm_free_user_pages_fork_stack_scenario);

    JARVIS_REGISTER_TEST(mempool_alloc_free);
    JARVIS_REGISTER_TEST(mempool_large_alloc);
    JARVIS_REGISTER_TEST(mempool_fragmentation);
    JARVIS_REGISTER_TEST(mempool_reuse);
}
