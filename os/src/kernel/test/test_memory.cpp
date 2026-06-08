#include <test.hpp>
#include <logger.hpp>
#include <constants.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>

using namespace kernel;

JARVIS_TEST(pmm_alloc_free) {
    uint64_t before = PMM::free_memory();
    uint64_t p1 = PMM::alloc_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(PMM::free_memory() == before - 4096);
    PMM::free_page(p1);
    JARVIS_ASSERT(PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_alloc_contiguous) {
    uint64_t before = PMM::free_memory();
    uint64_t pages = PMM::alloc_contiguous(4);
    JARVIS_ASSERT(pages != 0);
    JARVIS_ASSERT(PMM::free_memory() <= before - 4 * 4096);
    for (size_t i = 0; i < 4; ++i)
        PMM::free_page(pages + i * 4096);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_user_alloc) {
    uint64_t before = PMM::free_memory();
    uint64_t p1 = PMM::alloc_user_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(PMM::is_user_page(p1));
    PMM::free_page(p1);
    JARVIS_ASSERT(PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_total_memory) {
    uint64_t total = PMM::total_memory();
    JARVIS_ASSERT(total > 0);
    JARVIS_ASSERT(total >= PMM::free_memory());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vmm_free_user_pages_skips_kernel_owned_entries) {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    uint64_t user_page = PMM::alloc_user_page();
    JARVIS_ASSERT(user_page != 0);
    VMM::map_page_in_pml4(0x8000000000ULL, user_page, true, pml4);

    VMM::free_user_pages(pml4);

    PMM::free_page(pml4);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vmm_free_user_pages_fork_stack_scenario) {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    uint64_t stack_page = PMM::alloc_user_page();
    JARVIS_ASSERT(stack_page != 0);
    VMM::map_page_in_pml4(mem::STACK_VADDR, stack_page, true, pml4);

    VMM::free_user_pages(pml4);

    PMM::free_page(pml4);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_alloc_free) {
    void* p1 = MemPool::alloc(16);
    JARVIS_ASSERT(p1 != nullptr);
    void* p2 = MemPool::alloc(32);
    JARVIS_ASSERT(p2 != nullptr);
    JARVIS_ASSERT(p2 != p1);
    MemPool::free(p1);
    MemPool::free(p2);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_large_alloc) {
    void* p = MemPool::alloc(1024);
    JARVIS_ASSERT(p != nullptr);
    MemPool::free(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_fragmentation) {
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

JARVIS_TEST(mempool_reuse) {
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

JARVIS_TEST(vmm_huge_page_split_regression) {
    // Bug #3 regression: map_page through a 2 MiB huge page in the kernel HHDM
    // must split the huge page and correctly map the target 4 KiB page.
    //
    // We pick virtual address HHDM_OFFSET + 0x802000, which falls inside the
    // 2 MiB huge page at PD_HIGHER[4] (physical 8–10 MiB).  PD_HIGHER[4] is
    // backed by unused physical memory (kernel BSS ends at ~3.3 MiB), so it is
    // safe to split and remap temporarily.

    constexpr uint64_t test_vaddr = arch::HHDM_OFFSET + 0x802000; // PD_HIGHER[4], PT idx 2
    constexpr uint64_t huge_page_base = arch::HHDM_OFFSET + 0x800000;

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

    // Restore the original mapping so we don't leave the page tables in a
    // modified state.
    VMM::unmap_page(test_vaddr);
    VMM::map_page(test_vaddr, 0x802000, false);
    JARVIS_ASSERT(VMM::virt_to_phys(test_vaddr) == 0x802000);

    PMM::free_page(test_phys);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vmm_hhdm_access_consistency) {
    // Bugs #1/#2 regression: verify that PML4 is always accessed through
    // HHDM_OFFSET, never through the identity map (which is absent in user
    // PML4s and unreliable above 128 MiB).

    uint64_t kernel_pml4 = VMM::get_kernel_pml4();
    JARVIS_ASSERT(kernel_pml4 != 0);

    // virt_to_phys on a known kernel address must succeed (proves the HHDM
    // page-table walk works).
    uint64_t kpml4_virt = VMM::virt_to_phys(reinterpret_cast<uint64_t>(&VMM::init));
    JARVIS_ASSERT(kpml4_virt != 0);

    // Map a page through map_page (kernel PML4, HHDM code path) and verify.
    uint64_t p = PMM::alloc_page();
    JARVIS_ASSERT(p != 0);
    uint64_t v = arch::HHDM_OFFSET + 0x900000; // safe: PD_HIGHER[4], PT idx 0x100
    VMM::map_page(v, p, false);
    JARVIS_ASSERT(VMM::virt_to_phys(v) == (p & ~0xFFFULL));
    VMM::unmap_page(v);
    JARVIS_ASSERT(VMM::virt_to_phys(v) == 0);
    // Restore original mapping
    VMM::map_page(v, 0x900000, false);

    PMM::free_page(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_alloc_page_table) {
    // Page-table pages are allocated from a dedicated low-memory pool so the
    // HHDM (which only maps the first 128 MiB with huge pages) can reach them.
    // Verify that alloc_page_table returns a valid page below 128 MiB.
    uint64_t pt = PMM::alloc_page_table();
    JARVIS_ASSERT(pt != 0);
    JARVIS_ASSERT(pt < 128ULL * 1024 * 1024);
    PMM::free_page(pt);
    JARVIS_TEST_PASS();
}

void register_memory_tests() {
    Logger::info("Registering memory tests");

    JARVIS_REGISTER_TEST(pmm_alloc_free);
    JARVIS_REGISTER_TEST(pmm_alloc_contiguous);
    JARVIS_REGISTER_TEST(pmm_user_alloc);
    JARVIS_REGISTER_TEST(pmm_total_memory);
    JARVIS_REGISTER_TEST(pmm_alloc_page_table);
    JARVIS_REGISTER_TEST(vmm_huge_page_split_regression);
    JARVIS_REGISTER_TEST(vmm_hhdm_access_consistency);
    JARVIS_REGISTER_TEST(vmm_free_user_pages_skips_kernel_owned_entries);
    JARVIS_REGISTER_TEST(vmm_free_user_pages_fork_stack_scenario);

    JARVIS_REGISTER_TEST(mempool_alloc_free);
    JARVIS_REGISTER_TEST(mempool_large_alloc);
    JARVIS_REGISTER_TEST(mempool_fragmentation);
    JARVIS_REGISTER_TEST(mempool_reuse);
}
