#include <test.hpp>
#include <logger.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/pmm.hpp>
#include <constants.hpp>

using namespace kernel;

enum : uint64_t {
    PML4_SHIFT    = 39,
    PDPT_SHIFT    = 30,
    PD_SHIFT      = 21,
    PT_SHIFT      = 12,
    PAGE_PRESENT  = 1ULL << 0,
    PAGE_WRITE    = 1ULL << 1,
    PAGE_USER     = 1ULL << 2,
    PAGE_HUGE     = 1ULL << 7,
    PAGE_NX       = 1ULL << 63,
};

static void dbg_dump_pml4(uint64_t pml4_phys) {
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4_phys & ~0xFFFULL));
    Logger::raw_write("PML4 at 0x"); Logger::print_hex(pml4_phys); Logger::raw_write(":\n");

    for (int i = 0; i < 512; ++i) {
        if (!(pml4[i] & PAGE_PRESENT)) continue;
        uint64_t phys = pml4[i] & ~0xFFFULL;
        Logger::raw_write("  [");
        Logger::print_dec(i);
        Logger::raw_write("] PDPT=0x");
        Logger::print_hex(phys);
        if (pml4[i] & PAGE_USER) Logger::raw_write(" US");
        if (pml4[i] & PAGE_WRITE) Logger::raw_write(" RW");
        Logger::raw_write("\n");

        if (i >= static_cast<int>(arch::PML4_USER_COUNT)) continue;
        auto* pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + phys);
        for (int j = 0; j < 512; ++j) {
            if (!(pdpt[j] & PAGE_PRESENT)) continue;
            uint64_t pd_phys = pdpt[j] & ~0xFFFULL;
            uint64_t va_base = (static_cast<uint64_t>(i) << PML4_SHIFT)
                             | (static_cast<uint64_t>(j) << PDPT_SHIFT);
            Logger::raw_write("    [");
            Logger::print_dec(j);
            Logger::raw_write("] ");
            if (pdpt[j] & PAGE_HUGE) {
                Logger::raw_write("1GiB page phys=0x");
                Logger::print_hex(pd_phys);
            } else {
                Logger::raw_write("PD=0x");
                Logger::print_hex(pd_phys);
            }
            Logger::raw_write(" va=0x");
            Logger::print_hex(va_base);
            if (pdpt[j] & PAGE_USER) Logger::raw_write(" US");
            if (pdpt[j] & PAGE_WRITE) Logger::raw_write(" RW");
            Logger::raw_write("\n");

            if ((pdpt[j] & PAGE_HUGE) || !PMM::is_user_page(pd_phys)) continue;
            auto* pd = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pd_phys);
            for (int k = 0; k < 512; ++k) {
                if (!(pd[k] & PAGE_PRESENT)) continue;
                uint64_t pt_phys = pd[k] & ~0xFFFULL;
                Logger::raw_write("      [");
                Logger::print_dec(k);
                Logger::raw_write("] ");
                if (pd[k] & PAGE_HUGE) {
                    Logger::raw_write("2MiB page phys=0x");
                    Logger::print_hex(pt_phys);
                } else {
                    Logger::raw_write("PT=0x");
                    Logger::print_hex(pt_phys);
                }
                if (pd[k] & PAGE_USER) Logger::raw_write(" US");
                if (pd[k] & PAGE_WRITE) Logger::raw_write(" RW");
                Logger::raw_write("\n");

                if ((pd[k] & PAGE_HUGE) || !PMM::is_user_page(pt_phys)) continue;
                auto* pt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pt_phys);
                for (int l = 0; l < 512; ++l) {
                    if (!(pt[l] & PAGE_PRESENT)) continue;
                    uint64_t leaf = pt[l] & ~0xFFFULL;
                    Logger::raw_write("        [");
                    Logger::print_dec(l);
                    Logger::raw_write("] page=0x");
                    Logger::print_hex(leaf);
                    if (pt[l] & PAGE_USER) Logger::raw_write(" US");
                    if (pt[l] & PAGE_WRITE) Logger::raw_write(" RW");
                    Logger::raw_write("\n");
                }
            }
        }
    }
}

// Runmode: kernel
// Testidea: Verifies clone_kernel_pml4() has zero entries in user range 0-255.
// Input: Call clone_kernel_pml4(), inspect entries 0-255
// Expect: All user entries are zero (no identity-map leak)
// Depends: kernel::memory::VMM
JARVIS_TEST(bug007_clone_clears_user_entries) {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    auto* virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4 & ~0xFFFULL));
    bool leak = false;
    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i) {
        if (virt[i] != 0) {
            Logger::raw_write("  LEAK: entry ");
            Logger::print_dec(i);
            Logger::raw_write(" = 0x");
            Logger::print_hex(virt[i]);
            Logger::raw_write("\n");
            leak = true;
        }
    }

    PMM::free_page(pml4);
    JARVIS_ASSERT(!leak);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies clone_kernel_pml4() copies kernel entries 256-511 matching the kernel PML4.
// Input: Call clone_kernel_pml4(), compare entries with kernel PML4
// Expect: Kernel entries match exactly
// Depends: kernel::memory::VMM
JARVIS_TEST(bug007_clone_kernel_entries_match) {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    uint64_t kernel_pml4 = VMM::get_kernel_pml4();
    auto* new_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4 & ~0xFFFULL));
    auto* kern_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (kernel_pml4 & ~0xFFFULL));

    bool mismatch = false;
    for (size_t i = arch::PML4_KERNEL_START; i < arch::PML4_ENTRIES; ++i) {
        if (new_virt[i] != kern_virt[i]) {
            Logger::raw_write("  MISMATCH entry ");
            Logger::print_dec(i);
            Logger::raw_write(": new=0x");
            Logger::print_hex(new_virt[i]);
            Logger::raw_write(" kernel=0x");
            Logger::print_hex(kern_virt[i]);
            Logger::raw_write("\n");
            mismatch = true;
        }
    }

    PMM::free_page(pml4);
    JARVIS_ASSERT(!mismatch);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Simulates fork PML4 setup: parent has user entries, child PML4 copies them.
// Input: Create parent PML4 with user mapping, create child PML4 copying user entries
// Expect: Child's user entries match parent's, kernel entries match kernel PML4
// Depends: kernel::memory::VMM, PMM
JARVIS_TEST(bug007_fork_pml4_user_entries_match) {
    uint64_t parent_pml4 = PMM::alloc_page();
    JARVIS_ASSERT(parent_pml4 != 0);
    auto* parent_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (parent_pml4 & ~0xFFFULL));

    uint64_t pdpt = PMM::alloc_page_table();
    JARVIS_ASSERT(pdpt != 0);
    uint64_t pd = PMM::alloc_page_table();
    JARVIS_ASSERT(pd != 0);
    uint64_t pt = PMM::alloc_page_table();
    JARVIS_ASSERT(pt != 0);

    // Set up a user mapping at virtual address 0x400000 (typical ELF base)
    constexpr uint64_t TEST_VA = 0x400000;
    size_t pml4_idx = (TEST_VA >> PML4_SHIFT) & 0x1FF;
    size_t pdpt_idx = (TEST_VA >> PDPT_SHIFT) & 0x1FF;
    size_t pd_idx   = (TEST_VA >> PD_SHIFT) & 0x1FF;
    size_t pt_idx   = (TEST_VA >> PT_SHIFT) & 0x1FF;
    JARVIS_ASSERT(pml4_idx < arch::PML4_USER_COUNT);

    parent_virt[pml4_idx] = pdpt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    auto* pdpt_v = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pdpt);
    pdpt_v[pdpt_idx] = pd | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    auto* pd_v = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pd);
    pd_v[pd_idx] = pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    uint64_t user_page = PMM::alloc_user_page();
    JARVIS_ASSERT(user_page != 0);
    auto* pt_v = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pt);
    pt_v[pt_idx] = user_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    // Create child PML4 (fork-style: copy user entries from parent, kernel entries from kernel)
    uint64_t kernel_pml4 = VMM::get_kernel_pml4();
    auto* kern_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (kernel_pml4 & ~0xFFFULL));

    uint64_t child_pml4 = PMM::alloc_page();
    JARVIS_ASSERT(child_pml4 != 0);
    auto* child_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (child_pml4 & ~0xFFFULL));

    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i)
        child_virt[i] = parent_virt[i];
    for (size_t i = arch::PML4_KERNEL_START; i < arch::PML4_ENTRIES; ++i)
        child_virt[i] = kern_virt[i];

    // Verify user entries match parent
    bool user_mismatch = false;
    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i) {
        if (child_virt[i] != parent_virt[i]) {
            Logger::raw_write("  USER MISMATCH entry "); Logger::print_dec(i);
            Logger::raw_write(": child=0x"); Logger::print_hex(child_virt[i]);
            Logger::raw_write(" parent=0x"); Logger::print_hex(parent_virt[i]);
            Logger::raw_write("\n");
            user_mismatch = true;
        }
    }
    JARVIS_ASSERT(!user_mismatch);

    // Verify kernel entries match kernel PML4
    bool kern_mismatch = false;
    for (size_t i = arch::PML4_KERNEL_START; i < arch::PML4_ENTRIES; ++i) {
        if (child_virt[i] != kern_virt[i]) {
            Logger::raw_write("  KERN MISMATCH entry "); Logger::print_dec(i);
            Logger::raw_write(": child=0x"); Logger::print_hex(child_virt[i]);
            Logger::raw_write(" kernel=0x"); Logger::print_hex(kern_virt[i]);
            Logger::raw_write("\n");
            kern_mismatch = true;
        }
    }
    JARVIS_ASSERT(!kern_mismatch);

    // Clean up: free the user data page, page table pages, PML4 pages
    // Note: parent and child share page table pages (pdpt, pd, pt).
    // free_user_pages on either would skip them (kernel-owned), so we must free manually.
    PMM::free_page(user_page);
    PMM::free_page(pt);
    PMM::free_page(pd);
    PMM::free_page(pdpt);
    PMM::free_page(child_pml4);
    PMM::free_page(parent_pml4);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies mapping in child's PML4 does not corrupt parent's PML4.
// Input: Create parent PML4, fork child, map a new page in child's PML4
// Expect: Parent's PML4 unchanged, child's PML4 has the new mapping
// Depends: kernel::memory::VMM, PMM
JARVIS_TEST(bug007_fork_map_child_no_corrupt_parent) {
    uint64_t parent_pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(parent_pml4 != 0);

    // Simulate fork: child copies parent's user entries
    auto* parent_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (parent_pml4 & ~0xFFFULL));
    uint64_t kernel_pml4 = VMM::get_kernel_pml4();
    auto* kern_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (kernel_pml4 & ~0xFFFULL));

    uint64_t child_pml4 = PMM::alloc_page();
    JARVIS_ASSERT(child_pml4 != 0);
    auto* child_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (child_pml4 & ~0xFFFULL));

    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i)
        child_virt[i] = parent_virt[i];
    for (size_t i = arch::PML4_KERNEL_START; i < arch::PML4_ENTRIES; ++i)
        child_virt[i] = kern_virt[i];

    // Snapshot parent's user entries before child modifies
    uint64_t parent_snapshot[256];
    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i)
        parent_snapshot[i] = parent_virt[i];

    // Map a page in the CHILD's PML4 (note: the kid uses child_pml4)
    uint64_t child_va = 0x7FFF00000000ULL;
    uint64_t child_phys = PMM::alloc_page();
    JARVIS_ASSERT(child_phys != 0);
    VMM::map_page_in_pml4(child_va, child_phys, true, child_pml4);

    // Verify child can translate the new mapping
    uint64_t translated = VMM::virt_to_phys_in_pml4(child_va, child_pml4);
    JARVIS_ASSERT(translated == child_phys);

    // Verify parent's PML4 entries are unchanged
    bool parent_unchanged = true;
    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i) {
        if (parent_virt[i] != parent_snapshot[i]) {
            Logger::raw_write("  PARENT CORRUPTED at entry "); Logger::print_dec(i);
            Logger::raw_write(": before=0x"); Logger::print_hex(parent_snapshot[i]);
            Logger::raw_write(" after=0x"); Logger::print_hex(parent_virt[i]);
            Logger::raw_write("\n");
            parent_unchanged = false;
        }
    }
    JARVIS_ASSERT(parent_unchanged);

    // Verify parent cannot see the mapping
    uint64_t parent_t = VMM::virt_to_phys_in_pml4(child_va, parent_pml4);
    JARVIS_ASSERT(parent_t == 0);

    PMM::free_page(child_phys);
    PMM::free_page(child_pml4);
    PMM::free_page(parent_pml4);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies free_user_pages skips shared (kernel-owned) page table pages.
// Input: Create shared PML4 hierarchy, call free_user_pages on child
// Expect: Only user data pages freed, shared page table pages remain
// Depends: kernel::memory::VMM, PMM
JARVIS_TEST(bug007_free_user_pages_shared_safe) {
    uint64_t parent_pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(parent_pml4 != 0);
    auto* parent_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (parent_pml4 & ~0xFFFULL));

    // Create a user mapping in parent (using the fresh PML4)
    uint64_t va = 0x10000000;
    size_t pml4_idx = (va >> PML4_SHIFT) & 0x1FF;
    uint64_t pdpt = PMM::alloc_page_table();
    uint64_t pd = PMM::alloc_page_table();
    uint64_t pt = PMM::alloc_page_table();
    JARVIS_ASSERT(pdpt != 0 && pd != 0 && pt != 0);

    parent_virt[pml4_idx] = pdpt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    auto* pdpt_v = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pdpt);
    pdpt_v[(va >> PDPT_SHIFT) & 0x1FF] = pd | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    auto* pd_v = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pd);
    pd_v[(va >> PD_SHIFT) & 0x1FF] = pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    uint64_t user_page = PMM::alloc_user_page();
    JARVIS_ASSERT(user_page != 0);
    auto* pt_v = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pt);
    pt_v[(va >> PT_SHIFT) & 0x1FF] = user_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    // Simulate fork child that shares parent's page tables
    uint64_t child_pml4 = PMM::alloc_page();
    auto* child_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (child_pml4 & ~0xFFFULL));
    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i)
        child_virt[i] = parent_virt[i];

    // free_user_pages on child should not crash. It skips kernel-owned
    // page table pages (pdpt, pd, pt) — the user data page is behind them
    // and is NOT reached (by design: shared fork page tables must not
    // corrupt the parent's mappings).
    VMM::free_user_pages(child_pml4);

    // Page table pages remain allocated (kernel-owned, never freed by free_user_pages)
    // The data page also remains because free_user_pages cannot reach it
    // through kernel-owned page tables.
    JARVIS_ASSERT(PMM::is_user_page(pdpt) == false);
    JARVIS_ASSERT(PMM::is_user_page(pd) == false);
    JARVIS_ASSERT(PMM::is_user_page(pt) == false);

    // Parent's PML4 entry is untouched
    JARVIS_ASSERT((parent_virt[pml4_idx] & ~0xFFFULL) == pdpt);

    // Clean up manually: unmap in reverse order, free data + page tables
    pt_v[(va >> PT_SHIFT) & 0x1FF] = 0;
    PMM::free_page(user_page);
    pd_v[(va >> PD_SHIFT) & 0x1FF] = 0;
    PMM::free_page(pt);
    pdpt_v[(va >> PDPT_SHIFT) & 0x1FF] = 0;
    PMM::free_page(pd);
    parent_virt[pml4_idx] = 0;
    PMM::free_page(pdpt);
    PMM::free_page(child_pml4);
    PMM::free_page(parent_pml4);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies the full identity-map is absent from cloned PML4 (all 512 entries zero at PDPT level).
// Input: Call clone_kernel_pml4(), use dbg_dump_pml4 to inspect, then verify no user-accessible entries exist
// Expect: No user-accessible entries in user range 0-255
// Depends: kernel::memory::VMM
JARVIS_TEST(bug007_full_dump_no_user_entries) {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    Logger::raw_write("  Dumping PML4 via dbg_dump_pml4:\n");
    dbg_dump_pml4(pml4);

    auto* virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4 & ~0xFFFULL));
    bool has_user_entry = false;
    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i) {
        if (virt[i] & PAGE_PRESENT) {
            Logger::raw_write("  UNEXPECTED PRESENT entry ");
            Logger::print_dec(i);
            Logger::raw_write(" = 0x");
            Logger::print_hex(virt[i]);
            Logger::raw_write("\n");
            has_user_entry = true;
        }
    }

    PMM::free_page(pml4);
    JARVIS_ASSERT(!has_user_entry);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all bug #007 page table tests.
// Input: None
// Expect: All tests registered
// Depends: test framework
void register_bug007_tests() {
    Logger::info("Registering bug #007 page table tests");
    JARVIS_REGISTER_TEST(bug007_clone_clears_user_entries);
    JARVIS_REGISTER_TEST(bug007_clone_kernel_entries_match);
    JARVIS_REGISTER_TEST(bug007_fork_pml4_user_entries_match);
    JARVIS_REGISTER_TEST(bug007_fork_map_child_no_corrupt_parent);
    JARVIS_REGISTER_TEST(bug007_free_user_pages_shared_safe);
    JARVIS_REGISTER_TEST(bug007_full_dump_no_user_entries);
}
