#include <kernel/memory/vmm.hpp>
#include <kernel/memory/pmm.hpp>
#include <assert.hpp>

namespace kernel {

uint64_t VMM::kernel_pml4_ = 0;

void VMM::init() {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    kernel_pml4_ = cr3;
}

uint64_t* VMM::get_table(uint64_t* table, size_t index, bool create, bool user_alloc) {
    if (table[index] & PAGE_PRESENT) {
        return reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + (table[index] & ~0xFFFULL));
    }
    if (!create) return nullptr;

    uint64_t new_page = user_alloc ? PMM::alloc_user_page() : PMM::alloc_page();
    ASSERT(new_page != 0);

    auto* new_table = reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + new_page);
    for (size_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        new_table[i] = 0;
    }

    table[index] = new_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    return new_table;
}

void VMM::map_page(uint64_t virt_addr, uint64_t phys_addr, bool user) {
    auto* pml4 = reinterpret_cast<uint64_t*>(kernel_pml4_ & ~0xFFFULL);

    size_t pml4_idx = (virt_addr & PML4_MASK) >> PML4_SHIFT;
    size_t pdpt_idx = (virt_addr & PDPT_MASK) >> PDPT_SHIFT;
    size_t pd_idx   = (virt_addr & PD_MASK) >> PD_SHIFT;
    size_t pt_idx   = (virt_addr & PT_MASK) >> PT_SHIFT;

    auto* pdpt = get_table(pml4, pml4_idx, true);
    auto* pd   = get_table(pdpt, pdpt_idx, true);
    auto* pt   = get_table(pd, pd_idx, true);

    uint64_t flags = PAGE_PRESENT | PAGE_WRITE;
    if (user) flags |= PAGE_USER;

    pt[pt_idx] = phys_addr | flags;
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void VMM::unmap_page(uint64_t virt_addr) {
    auto* pml4 = reinterpret_cast<uint64_t*>(kernel_pml4_ & ~0xFFFULL);

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
    auto* pml4 = reinterpret_cast<uint64_t*>(kernel_pml4_ & ~0xFFFULL);

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
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void VMM::map_page_in_pml4(uint64_t virt_addr, uint64_t phys_addr,
                            bool user, uint64_t pml4_phys)
{
    auto* pml4 = reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + (pml4_phys & ~0xFFFULL));

    size_t pml4_idx = (virt_addr & PML4_MASK) >> PML4_SHIFT;
    size_t pdpt_idx = (virt_addr & PDPT_MASK) >> PDPT_SHIFT;
    size_t pd_idx   = (virt_addr & PD_MASK) >> PD_SHIFT;
    size_t pt_idx   = (virt_addr & PT_MASK) >> PT_SHIFT;

    auto* pdpt = get_table(pml4, pml4_idx, true, true);
    auto* pd   = get_table(pdpt, pdpt_idx, true, true);
    auto* pt   = get_table(pd, pd_idx, true, true);

    uint64_t flags = PAGE_PRESENT | PAGE_WRITE;
    if (user) flags |= PAGE_USER;

    pt[pt_idx] = phys_addr | flags;
}

uint64_t VMM::clone_kernel_pml4() {
    uint64_t phys = PMM::alloc_page();
    if (!phys) return 0;

    auto* src = reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + (kernel_pml4_ & ~0xFFFULL));
    auto* dst = reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + phys);
    for (size_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        dst[i] = src[i];
    }
    return phys;
}

void VMM::free_user_pages(uint64_t pml4_phys) {
    auto* pml4 = reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + (pml4_phys & ~0xFFFULL));
    for (int pml4_idx = 0; pml4_idx < 256; ++pml4_idx) {
        if (!(pml4[pml4_idx] & PAGE_PRESENT)) continue;
        uint64_t pdpt_phys = pml4[pml4_idx] & ~0xFFFULL;
        ASSERT(PMM::is_user_page(pdpt_phys));
        auto* pdpt = reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + pdpt_phys);
        for (int pdpt_idx = 0; pdpt_idx < 512; ++pdpt_idx) {
            if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) continue;
            if (pdpt[pdpt_idx] & PAGE_HUGE) {
                uint64_t page = pdpt[pdpt_idx] & ~0x3FFFFFULL;
                ASSERT(PMM::is_user_page(page));
                PMM::free_page(page);
                continue;
            }
            uint64_t pd_phys = pdpt[pdpt_idx] & ~0xFFFULL;
            ASSERT(PMM::is_user_page(pd_phys));
            auto* pd = reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + pd_phys);
            for (int pd_idx = 0; pd_idx < 512; ++pd_idx) {
                if (!(pd[pd_idx] & PAGE_PRESENT)) continue;
                if (pd[pd_idx] & PAGE_HUGE) {
                    uint64_t page = pd[pd_idx] & ~0x1FFFFFULL;
                    ASSERT(PMM::is_user_page(page));
                    PMM::free_page(page);
                    continue;
                }
                uint64_t pt_phys = pd[pd_idx] & ~0xFFFULL;
                ASSERT(PMM::is_user_page(pt_phys));
                auto* pt = reinterpret_cast<uint64_t*>(0xFFFF800000000000ULL + pt_phys);
                for (int pt_idx = 0; pt_idx < 512; ++pt_idx) {
                    if (!(pt[pt_idx] & PAGE_PRESENT)) continue;
                    uint64_t leaf = pt[pt_idx] & ~0xFFFULL;
                    ASSERT(PMM::is_user_page(leaf));
                    PMM::free_page(leaf);
                }
                PMM::free_page(pt_phys);
            }
            PMM::free_page(pd_phys);
        }
        PMM::free_page(pdpt_phys);
        pml4[pml4_idx] = 0;
    }
    asm volatile("invlpg (%0)" : : "r"(0) : "memory");
}

} // namespace kernel
