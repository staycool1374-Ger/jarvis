#include <kernel/memory/vmm.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>
#include <assert.hpp>
#include <kernel/memory/vmm_errors.hpp>

namespace kernel {

uint64_t VMM::kernel_pml4_ = 0;

void VMM::init() {
    kernel_pml4_ = arch::read_cr3();

    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));

    // Zero unused entries in all boot-constructed page tables so the page
    // table walker never follows uninitialised garbage pointers.  The boot
    // code only sets entry 0 in the PDPTs and entries 0-63 in the PDs;
    // the rest may contain residual GRUB/BIOS data with PAGE_PRESENT set.

    // Zero PDPT_IDENTITY[1-511]
    {
        uint64_t pdpt_phys = pml4[0] & ~0xFFFULL;
        auto* pdpt_ident = reinterpret_cast<uint64_t*>(
            arch::HHDM_OFFSET + pdpt_phys);
        for (size_t i = 1; i < PAGE_TABLE_ENTRIES; ++i) pdpt_ident[i] = 0;
    }

    // Zero PDPT_HIGHER[1-511]
    {
        uint64_t pdpt_phys = pml4[256] & ~0xFFFULL;
        auto* pdpt_higher = reinterpret_cast<uint64_t*>(
            arch::HHDM_OFFSET + pdpt_phys);
        for (size_t i = 1; i < PAGE_TABLE_ENTRIES; ++i) pdpt_higher[i] = 0;
    }

    // Zero PD_IDENTITY[64-511] (entries 0-63 are valid huge pages)
    {
        auto* pdpt_ident_p = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
            pml4[0] & ~0xFFFULL));
        auto* pd_ident = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
            pdpt_ident_p[0] & ~0xFFFULL));
        for (size_t i = 64; i < PAGE_TABLE_ENTRIES; ++i) pd_ident[i] = 0;
    }

    // Zero PD_HIGHER[64-511] (entries 0-63 are valid huge pages)
    {
        auto* pdpt_higher_p = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
            pml4[256] & ~0xFFFULL));
        auto* pd_higher = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
            pdpt_higher_p[0] & ~0xFFFULL));
        for (size_t i = 64; i < PAGE_TABLE_ENTRIES; ++i) pd_higher[i] = 0;
    }

    // Zero unused PML4 entries [1-255, 257-511] so get_table never follows
    // residual UEFI/GRUB data left by the bootloader.  The boot code only
    // sets PML4[0] and PML4[256]; the rest may contain garbage.
    for (size_t i = 1; i < PAGE_TABLE_ENTRIES; ++i) {
        if (i == 256) continue;
        pml4[i] = 0;
    }
}

uint64_t* VMM::get_table(uint64_t* table, size_t index, bool create,
    bool user_alloc) {
    if (table[index] & PAGE_PRESENT) {
        if (table[index] & PAGE_HUGE) {
            if (!create) return nullptr;
            uint64_t new_page = PMM::alloc_page_table();
            ENSURE(new_page != 0);
            auto* new_table = reinterpret_cast<uint64_t*>(arch::
                HHDM_OFFSET + new_page);
            uint64_t huge_base  = table[index] & ~0x1FFFFFULL;
            uint64_t base_flags = table[index] & (
                PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
            for (size_t i = 0; i < 512; ++i) {
                new_table[i] = (huge_base + i * 0x1000) | base_flags;
            }
            table[index] = new_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
            return new_table;
        }
        return reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (table[index
            ] & ~0xFFFULL));
    }
    if (!create) return nullptr;

    uint64_t new_page = user_alloc ? PMM::alloc_user_page() : PMM::
        alloc_page_table();
    ENSURE(new_page != 0);

    auto* new_table = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + new_page);
    for (size_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        new_table[i] = 0;
    }

    table[index] = new_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    return new_table;
}

void VMM::map_page(uint64_t virt_addr, uint64_t phys_addr, bool user) {
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));

    size_t pml4_idx = (virt_addr & PML4_MASK) >> PML4_SHIFT;
    size_t pdpt_idx = (virt_addr & PDPT_MASK) >> PDPT_SHIFT;
    size_t pd_idx   = (virt_addr & PD_MASK) >> PD_SHIFT;
    size_t pt_idx   = (virt_addr & PT_MASK) >> PT_SHIFT;

    auto* pdpt = get_table(pml4, pml4_idx, true);
    auto* pd   = get_table(pdpt, pdpt_idx, true);

    // If the PD entry is a 2MB huge page, split it into 512 4KB entries
    // so we can map an individual 4KB page within it.
    if (pd[pd_idx] & PAGE_HUGE) {
        uint64_t new_pt_phys = PMM::alloc_page_table();
        ENSURE(new_pt_phys != 0);
        auto* new_pt = reinterpret_cast<uint64_t*>(arch::
            HHDM_OFFSET + new_pt_phys);
        uint64_t huge_base  = pd[pd_idx] & ~0x1FFFFFULL;
        uint64_t base_flags = pd[pd_idx] & (
            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        for (size_t i = 0; i < 512; ++i) {
            new_pt[i] = (huge_base + i * 0x1000) | base_flags;
        }
        pd[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    auto* pt   = get_table(pd, pd_idx, true);

    uint64_t flags = PAGE_PRESENT | PAGE_WRITE;
    if (user) flags |= PAGE_USER;

    pt[pt_idx] = phys_addr | flags;
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void VMM::unmap_page(uint64_t virt_addr) {
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));

    size_t pml4_idx = (virt_addr & PML4_MASK) >> PML4_SHIFT;
    size_t pdpt_idx = (virt_addr & PDPT_MASK) >> PDPT_SHIFT;
    size_t pd_idx   = (virt_addr & PD_MASK) >> PD_SHIFT;
    size_t pt_idx   = (virt_addr & PT_MASK) >> PT_SHIFT;

    auto* pdpt = get_table(pml4, pml4_idx, false);
    if (!pdpt) return;
    auto* pd = get_table(pdpt, pdpt_idx, false);
    if (!pd) return;
    auto* pt = get_table(pd, pd_idx, false);
    if (!pt) return;

    pt[pt_idx] = 0;
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

uint64_t VMM::virt_to_phys(uint64_t virt_addr) {
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));

    size_t pml4_idx = (virt_addr & PML4_MASK) >> PML4_SHIFT;
    size_t pdpt_idx = (virt_addr & PDPT_MASK) >> PDPT_SHIFT;
    size_t pd_idx   = (virt_addr & PD_MASK) >> PD_SHIFT;
    size_t pt_idx   = (virt_addr & PT_MASK) >> PT_SHIFT;

    auto* pdpt = get_table(pml4, pml4_idx, false);
    if (!pdpt) return 0;
    auto* pd = get_table(pdpt, pdpt_idx, false);
    if (!pd) return 0;

    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & ~0x1FFFFFULL) + (virt_addr & 0x1FFFFF);
    }

    auto* pt = get_table(pd, pd_idx, false);
    if (!pt || !(pt[pt_idx] & PAGE_PRESENT)) return 0;

    return (pt[pt_idx] & ~0xFFFULL) + (virt_addr & 0xFFF);
}

uint64_t VMM::current_pml4() {
    return arch::read_cr3();
}

void VMM::map_page_in_pml4(uint64_t virt_addr, uint64_t phys_addr,
                            bool user, uint64_t pml4_phys)
{
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        pml4_phys & ~0xFFFULL));

    size_t pml4_idx = (virt_addr & PML4_MASK) >> PML4_SHIFT;
    size_t pdpt_idx = (virt_addr & PDPT_MASK) >> PDPT_SHIFT;
    size_t pd_idx   = (virt_addr & PD_MASK) >> PD_SHIFT;
    size_t pt_idx   = (virt_addr & PT_MASK) >> PT_SHIFT;

    auto* pdpt = get_table(pml4, pml4_idx, true, true);
    auto* pd   = get_table(pdpt, pdpt_idx, true, true);

    if (pd[pd_idx] & PAGE_HUGE) {
        uint64_t new_pt_phys = PMM::alloc_page_table();
        ENSURE(new_pt_phys != 0);
        auto* new_pt = reinterpret_cast<uint64_t*>(arch::
            HHDM_OFFSET + new_pt_phys);
        uint64_t huge_base  = pd[pd_idx] & ~0x1FFFFFULL;
        uint64_t base_flags = pd[pd_idx] & (
            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        for (size_t i = 0; i < 512; ++i) {
            new_pt[i] = (huge_base + i * 0x1000) | base_flags;
        }
        pd[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    auto* pt   = get_table(pd, pd_idx, true, true);

    uint64_t flags = PAGE_PRESENT | PAGE_WRITE;
    if (user) flags |= PAGE_USER;

    pt[pt_idx] = phys_addr | flags;
}

uint64_t VMM::clone_kernel_pml4() {
    uint64_t phys = PMM::alloc_page();
    if (!phys) { ASSERT(errors::VmmError::VMM_ERR_PML4_ALLOC); return 0; }

    auto* src = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));
    auto* dst = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + phys);
// Clear user-space entries (0-255) — the kernel's boot identity-map must not
// leak into user page tables.  Fork shares user entries by copying them from
    // the PARENT's PML4, not from the kernel PML4.
    for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i) {
        dst[i] = 0;
    }
// Copy kernel-space entries (256-511) so user tasks can access kernel mappings.
    for (size_t i = arch::PML4_KERNEL_START; i < PAGE_TABLE_ENTRIES; ++i) {
        dst[i] = src[i];
    }
    return phys;
}

void VMM::free_user_pages(uint64_t pml4_phys) {
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        pml4_phys & ~0xFFFULL));
    for (int pml4_idx = 0; pml4_idx < static_cast<int>(arch::PML4_USER_COUNT
        ); ++pml4_idx) {
        if (!(pml4[pml4_idx] & PAGE_PRESENT)) continue;
        uint64_t pdpt_phys = pml4[pml4_idx] & ~0xFFFULL;
        if (!PMM::is_user_page(pdpt_phys)) continue;
        auto* pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pdpt_phys);
        for (int pdpt_idx = 0; pdpt_idx < 512; ++pdpt_idx) {
            if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) continue;
            if (pdpt[pdpt_idx] & PAGE_HUGE) {
                uint64_t page = pdpt[pdpt_idx] & ~0x3FFFFFULL;
                if (!PMM::is_user_page(page)) continue;
                PMM::free_page(page);
                continue;
            }
            uint64_t pd_phys = pdpt[pdpt_idx] & ~0xFFFULL;
            if (!PMM::is_user_page(pd_phys)) continue;
            auto* pd = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pd_phys);
            for (int pd_idx = 0; pd_idx < 512; ++pd_idx) {
                if (!(pd[pd_idx] & PAGE_PRESENT)) continue;
                if (pd[pd_idx] & PAGE_HUGE) {
                    uint64_t page = pd[pd_idx] & ~0x1FFFFFULL;
                    if (!PMM::is_user_page(page)) continue;
                    PMM::free_page(page);
                    continue;
                }
                uint64_t pt_phys = pd[pd_idx] & ~0xFFFULL;
                if (!PMM::is_user_page(pt_phys)) continue;
                auto* pt = reinterpret_cast<uint64_t*>(arch::
                    HHDM_OFFSET + pt_phys);
                for (int pt_idx = 0; pt_idx < 512; ++pt_idx) {
                    if (!(pt[pt_idx] & PAGE_PRESENT)) continue;
                    uint64_t leaf = pt[pt_idx] & ~0xFFFULL;
                    if (!PMM::is_user_page(leaf)) continue;
                    PMM::free_page(leaf);
                }
                PMM::free_page(pt_phys);
            }
            PMM::free_page(pd_phys);
        }
        PMM::free_page(pdpt_phys);
        pml4[pml4_idx] = 0;
    }
    // TLB flush is the caller's responsibility (via CR3 reload)
}

uint64_t VMM::virt_to_phys_in_pml4(uint64_t virt_addr, uint64_t pml4_phys) {
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        pml4_phys & ~0xFFFULL));

    size_t pml4_idx = (virt_addr & PML4_MASK) >> PML4_SHIFT;
    size_t pdpt_idx = (virt_addr & PDPT_MASK) >> PDPT_SHIFT;
    size_t pd_idx   = (virt_addr & PD_MASK) >> PD_SHIFT;
    size_t pt_idx   = (virt_addr & PT_MASK) >> PT_SHIFT;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    auto* pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4[pml4_idx
        ] & ~0xFFFULL));

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;
    auto* pd = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pdpt[pdpt_idx
        ] & ~0xFFFULL));

    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & ~0x1FFFFFULL) + (virt_addr & 0x1FFFFF);
    }

    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    auto* pt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pd[pd_idx
        ] & ~0xFFFULL));

    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFFULL) + (virt_addr & 0xFFF);
}

} // namespace kernel
