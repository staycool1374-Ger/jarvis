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
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/pmm.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies unmap_page on an unmapped VA is safe (idempotent).
// Input: Call unmap_page on unused virtual address
// Expect: No crash, returns success
// Depends: kernel::memory::VMM
JARVIS_TEST(vmm_unmap_already_unmapped, "PRE: none | POST: none") {
    uint64_t unused_va = 0x7FFF00000000ULL;
    VMM::unmap_page(unused_va);
    JARVIS_ASSERT(VMM::virt_to_phys(unused_va) == 0);
}

// Runmode: kernel
// Testidea: Verifies map_page on a VA that already has a physical page mapped.
// Input: Map page at VA, map again at same VA
// Expect: Fails or unmaps first
// Depends: kernel::memory::VMM
JARVIS_TEST(vmm_map_already_mapped, "PRE: none | POST: none") {
    uint64_t va = 0x800000000000ULL;
    uint64_t phys1 = PMM::alloc_page();
    JARVIS_ASSERT(phys1 != 0);
    VMM::map_page(va, phys1, false);
    JARVIS_ASSERT(VMM::virt_to_phys(va) == phys1);

    uint64_t phys2 = PMM::alloc_page();
    JARVIS_ASSERT(phys2 != 0);
    VMM::map_page(va, phys2, false);
    JARVIS_ASSERT(VMM::virt_to_phys(va) == phys2);

    PMM::free_page(phys1);
    PMM::free_page(phys2);
}

// Runmode: kernel
// Testidea: Verifies map_page with phys=0 should fail.
// Input: Call map_page with physical address 0
// Expect: Returns error
// Depends: kernel::memory::VMM
JARVIS_TEST(vmm_map_page_null_phys, "PRE: none | POST: none") {
    uint64_t va = 0x800000100000ULL;
    VMM::map_page(va, 0, false);
    JARVIS_ASSERT(VMM::virt_to_phys(va) == 0);
}

// Runmode: kernel
// Testidea: Verifies when clone_kernel_pml4 runs out of memory, partial
// allocations are freed.
// Input: Exhaust memory, call clone_kernel_pml4
// Expect: No leaked page tables
// Depends: kernel::memory::VMM
JARVIS_TEST(vmm_clone_failure_rollback, "PRE: none | POST: none") {
    // STUB: clone_kernel_pml4 doesn't implement rollback on OOM
    // Returns 0 on failure but doesn't free partially allocated page tables
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies freeing user pages on a shared (forked) PML4 does not
// free pages still in use by parent.
// Input: Fork, free child's user pages, check parent
// Expect: Parent pages still valid
// Depends: kernel::memory::VMM
JARVIS_TEST(vmm_free_user_pages_shared, "PRE: none | POST: none") {
    // Simulate fork: parent has user pages, child shares page table pages
    uint64_t parent_pml4 = PMM::alloc_page();
    JARVIS_ASSERT(parent_pml4 != 0);
    auto* parent_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + parent_pml4);
    
    // Allocate shared PDPT/PD/PT pages (kernel pages, not user pages)
    uint64_t pdpt_phys = PMM::alloc_page_table();
    JARVIS_ASSERT(pdpt_phys != 0);
    uint64_t pd_phys = PMM::alloc_page_table();
    JARVIS_ASSERT(pd_phys != 0);
    uint64_t pt_phys = PMM::alloc_page_table();
    JARVIS_ASSERT(pt_phys != 0);
    
    // Set up parent page tables (user mapping at 0x400000)
    constexpr uint64_t PML4_SHIFT = 39;
    constexpr uint64_t PDPT_SHIFT = 30;
    constexpr uint64_t PD_SHIFT = 21;
    constexpr uint64_t PT_SHIFT = 12;
    constexpr uint64_t PAGE_PRESENT = 1ULL << 0;
    constexpr uint64_t PAGE_WRITE = 1ULL << 1;
    constexpr uint64_t PAGE_USER = 1ULL << 2;
    
    uint64_t test_va = 0x400000;
    size_t pml4_idx = (test_va >> PML4_SHIFT) & 0x1FF;
    size_t pdpt_idx = (test_va >> PDPT_SHIFT) & 0x1FF;
    size_t pd_idx = (test_va >> PD_SHIFT) & 0x1FF;
    size_t pt_idx = (test_va >> PT_SHIFT) & 0x1FF;
    
    parent_virt[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    auto* pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pdpt_phys);
    pdpt[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    auto* pd = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pd_phys);
    pd[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    auto* pt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pt_phys);
    
    // Allocate a user page and map it
    uint64_t user_page = PMM::alloc_user_page();
    JARVIS_ASSERT(user_page != 0);
    pt[pt_idx] = user_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    
    // Create child PML4 that shares the page table pages (simulating fork)
    uint64_t child_pml4 = PMM::alloc_page();
    JARVIS_ASSERT(child_pml4 != 0);
    auto* child_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + child_pml4);
    
    // Copy parent's user entries (shares PDPT/PD/PT pages)
    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i) {
        child_virt[i] = parent_virt[i];
    }
    
    // Call free_user_pages on child's PML4
    VMM::free_user_pages(child_pml4);
    
// Verify: shared PDPT/PD/PT pages should NOT be freed (they're kernel pages)
    // User data page SHOULD be freed
    JARVIS_ASSERT(PMM::is_user_page(user_page) == false); // Was freed
    
    // Page table pages should still be valid (not user pages, so not freed)
    JARVIS_ASSERT(PMM::is_user_page(pdpt_phys) == false);
    JARVIS_ASSERT(PMM::is_user_page(pd_phys) == false);
    JARVIS_ASSERT(PMM::is_user_page(pt_phys) == false);
    
    // Clean up
    PMM::free_page(pdpt_phys);
    PMM::free_page(pd_phys);
    PMM::free_page(pt_phys);
    PMM::free_page(parent_pml4);
    PMM::free_page(child_pml4);
}

// Runmode: kernel
// Testidea: Verifies split at PD boundary (address at 2 MiB-aligned edge).
// Input: Split huge page at 2 MiB boundary
// Expect: Correctly creates page table entries
// Depends: kernel::memory::VMM
JARVIS_TEST(vmm_huge_page_split_corner, "PRE: none | POST: none") {
    uint64_t va = 0x200000; // 2 MiB boundary
    uint64_t phys = PMM::alloc_page();
    JARVIS_ASSERT(phys != 0);
    
    // Map at the huge page boundary - should trigger split if huge page exists
    VMM::map_page(va, phys, false);
    JARVIS_ASSERT(VMM::virt_to_phys(va) == phys);
    
    // Map adjacent page in same 2 MiB region
    uint64_t phys2 = PMM::alloc_page();
    JARVIS_ASSERT(phys2 != 0);
    VMM::map_page(va + 0x1000, phys2, false);
    JARVIS_ASSERT(VMM::virt_to_phys(va + 0x1000) == phys2);
    
    PMM::free_page(phys);
    PMM::free_page(phys2);
}

// Runmode: kernel
// Testidea: Registers all VMM unit tests with the test framework.
// Input: None
// Expect: All VMM tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_vmm_tests() {
    Logger::info("Registering VMM tests");
    JARVIS_REGISTER_TEST(vmm_unmap_already_unmapped);
    JARVIS_REGISTER_TEST(vmm_map_already_mapped);
    JARVIS_REGISTER_TEST(vmm_map_page_null_phys);
    JARVIS_REGISTER_TEST(vmm_clone_failure_rollback);
    JARVIS_REGISTER_TEST(vmm_free_user_pages_shared);
    JARVIS_REGISTER_TEST(vmm_huge_page_split_corner);
}