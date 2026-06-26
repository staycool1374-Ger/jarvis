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

#include <kernel/memory/vmm.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>
#include <kernel/arch/page_table.hpp>
#include <assert.hpp>
#include <kernel/memory/vmm_errors.hpp>

namespace kernel {

uint64_t VMM::kernel_pml4_ = 0;

void VMM::init() {
    kernel_pml4_ = arch::read_cr3();

#if defined(CONFIG_ARCH_X86_64)
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
#elif defined(CONFIG_ARCH_AARCH64)
    // Boot.S already set up page tables.  Identity map uses 2048 L2 entries
    // across 4 tables (0-4GB).  Higher half has 2 L2 entries for kernel
    // (0xFFFF800040000000-0xFFFF800040400000).  VMM zeroing below is
    // x86_64-specific (PDPT/PD structure mismatch) and would corrupt the
    // identity L1[1] entry needed for kernel code at PA 0x40080000.
#elif defined(CONFIG_ARCH_RISCV64)
    // Boot.S already set up Sv39 page tables with HHDM mapping.
    // Kernel maps PA 0x80200000+ to VA 0xFFFFFFC080200000+ via 2MB pages.
    // No additional zeroing needed.
#endif
}

uint64_t* VMM::get_table(uint64_t* table, size_t index, bool create,
    bool user_alloc) {
    if (table[index] & PAGE_PRESENT) {
        if (
#if defined(CONFIG_ARCH_AARCH64)
            (table[index] & (PAGE_PRESENT | PAGE_TABLE)) == PAGE_PRESENT
#elif defined(CONFIG_ARCH_RISCV64)
            // For RISC-V, table entry has V=1, R=W=X=0
            (table[index] & (PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC)) == PAGE_PRESENT
#else
            table[index] & PAGE_HUGE
#endif
        ) {
            if (!create) return nullptr;
            uint64_t new_page = PMM::alloc_page_table();
            ENSURE(new_page != 0);
            auto* new_table = reinterpret_cast<uint64_t*>(arch::
                HHDM_OFFSET + new_page);
            uint64_t huge_base  = table[index] & ~0x1FFFFFULL;
#if defined(CONFIG_ARCH_AARCH64)
            uint64_t base_flags = table[index] & (PAGE_PRESENT | PAGE_AF);
            for (size_t i = 0; i < 512; ++i) {
                new_table[i] = (huge_base + i * 0x1000) | base_flags |
                    PAGE_TABLE;
            }
            table[index] = new_page | PAGE_PRESENT | PAGE_TABLE;
#elif defined(CONFIG_ARCH_RISCV64)
            // For RISC-V, 2MB block entry has V=1, R=1, W=1, X=1 (leaf)
            // Need to split into 512 4KB entries
            uint64_t base_flags = table[index] & (PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_USER | PAGE_GLOBAL | PAGE_ACCESSED | PAGE_DIRTY);
            for (size_t i = 0; i < 512; ++i) {
                new_table[i] = (huge_base + i * 0x1000) | base_flags | PAGE_ACCESSED | PAGE_DIRTY;
            }
            // Table entry: V=1, no RWX = points to next level table
            table[index] = new_page | PAGE_PRESENT;
#else
            uint64_t base_flags = table[index] & (
                PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
            for (size_t i = 0; i < 512; ++i) {
                new_table[i] = (huge_base + i * 0x1000) | base_flags;
            }
            table[index] = new_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
#endif
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

#if defined(CONFIG_ARCH_AARCH64)
    table[index] = new_page | PAGE_PRESENT | PAGE_TABLE;
#elif defined(CONFIG_ARCH_RISCV64)
    // Table entry: V=1, R=W=X=0 points to next level
    table[index] = new_page | PAGE_PRESENT;
#else
    table[index] = new_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
#endif

    return new_table;
}

void VMM::map_page(uint64_t virt_addr, uint64_t phys_addr, bool user) {
#if defined(CONFIG_ARCH_RISCV64)
    // Sv39 3-level page table walk
    auto* l0 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));

    size_t l0_idx = (virt_addr & VMM::L0_MASK) >> VMM::L0_SHIFT;
    size_t l1_idx = (virt_addr & VMM::L1_MASK) >> VMM::L1_SHIFT;
    size_t l2_idx = (virt_addr & VMM::L2_MASK) >> VMM::L2_SHIFT;

    auto* l1 = get_table(l0, l0_idx, true);
    auto* l2 = get_table(l1, l1_idx, true);

    // If L2 entry is a 2MB block (V=1, R|W|X=1), split it
    if ((l2[l2_idx] & PAGE_PRESENT) &&
        (l2[l2_idx] & (PAGE_READ | PAGE_WRITE | PAGE_EXEC))) {
        uint64_t new_l3_phys = PMM::alloc_page_table();
        ENSURE(new_l3_phys != 0);
        auto* new_l3 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + new_l3_phys);
        uint64_t block_base = l2[l2_idx] & ~0x1FFFFFULL;
        uint64_t base_flags = l2[l2_idx] & (PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_USER | PAGE_GLOBAL | PAGE_ACCESSED | PAGE_DIRTY);
        for (size_t i = 0; i < 512; ++i) {
            new_l3[i] = (block_base + i * 0x1000) | base_flags | PAGE_ACCESSED | PAGE_DIRTY;
        }
        // Table entry: V=1, no RWX
        l2[l2_idx] = new_l3_phys | PAGE_PRESENT;
    }

    auto* l3 = get_table(l2, l2_idx, true);

    uint64_t flags = PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    if (user) flags |= PAGE_USER;
    // Always set A/D bits
    flags |= PAGE_ACCESSED | PAGE_DIRTY;

    l3[l2_idx] = phys_addr | flags;
    arch::ArchPageTable::tlb_flush(virt_addr);
#else
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
#if defined(CONFIG_ARCH_AARCH64)
    if ((pd[pd_idx] & (PAGE_PRESENT | PAGE_TABLE)) == PAGE_PRESENT)
#else
    if (pd[pd_idx] & PAGE_HUGE)
#endif
    {
        uint64_t new_pt_phys = PMM::alloc_page_table();
        ENSURE(new_pt_phys != 0);
        auto* new_pt = reinterpret_cast<uint64_t*>(arch::
            HHDM_OFFSET + new_pt_phys);
        uint64_t huge_base  = pd[pd_idx] & ~0x1FFFFFULL;
#if defined(CONFIG_ARCH_AARCH64)
        uint64_t base_flags = pd[pd_idx] & (PAGE_PRESENT | PAGE_AF);
#else
        uint64_t base_flags = pd[pd_idx] & (
            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
#endif
        for (size_t i = 0; i < 512; ++i) {
            new_pt[i] = (huge_base + i * 0x1000) | base_flags;
        }
#if defined(CONFIG_ARCH_AARCH64)
        pd[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_TABLE | PAGE_AF;
#else
        pd[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
#endif
    }

    auto* pt   = get_table(pd, pd_idx, true);

#if defined(CONFIG_ARCH_AARCH64)
    uint64_t flags = PAGE_PRESENT | PAGE_TABLE | PAGE_AF;
    if (user) flags |= PAGE_AP_USER;
#else
    uint64_t flags = PAGE_PRESENT | PAGE_WRITE;
    if (user) flags |= PAGE_USER;
#endif

    pt[pt_idx] = phys_addr | flags;
    arch::ArchPageTable::tlb_flush(virt_addr);
#endif
}

void VMM::unmap_page(uint64_t virt_addr) {
#if defined(CONFIG_ARCH_RISCV64)
    auto* l0 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));

    size_t l0_idx = (virt_addr & VMM::L0_MASK) >> VMM::L0_SHIFT;
    size_t l1_idx = (virt_addr & VMM::L1_MASK) >> VMM::L1_SHIFT;
    size_t l2_idx = (virt_addr & VMM::L2_MASK) >> VMM::L2_SHIFT;

    auto* l1 = get_table(l0, l0_idx, false);
    if (!l1) return;
    auto* l2 = get_table(l1, l1_idx, false);
    if (!l2) return;
    auto* l3 = get_table(l2, l2_idx, false);
    if (!l3) return;

    l3[l2_idx] = 0;
    arch::ArchPageTable::tlb_flush(virt_addr);
#else
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
    arch::ArchPageTable::tlb_flush(virt_addr);
#endif
}

uint64_t VMM::virt_to_phys(uint64_t virt_addr) {
#if defined(CONFIG_ARCH_RISCV64)
    auto* l0 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));

    size_t l0_idx = (virt_addr & VMM::L0_MASK) >> VMM::L0_SHIFT;
    size_t l1_idx = (virt_addr & VMM::L1_MASK) >> VMM::L1_SHIFT;
    size_t l2_idx = (virt_addr & VMM::L2_MASK) >> VMM::L2_SHIFT;

    auto* l1 = get_table(l0, l0_idx, false);
    if (!l1) return 0;
    auto* l2 = get_table(l1, l1_idx, false);
    if (!l2) return 0;

    // Check for 2MB block mapping (leaf at L1 level - V=1, R|W|X=1)
    if ((l2[l1_idx] & PAGE_PRESENT) &&
        (l2[l1_idx] & (PAGE_READ | PAGE_WRITE | PAGE_EXEC))) {
        return (l2[l1_idx] & ~0x1FFFFFULL) + (virt_addr & 0x1FFFFF);
    }

    auto* l3 = get_table(l2, l2_idx, false);
    if (!l3 || !(l3[l2_idx] & PAGE_PRESENT)) return 0;

    return (l3[l2_idx] & ~0xFFFULL) + (virt_addr & 0xFFF);
#else
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

#if defined(CONFIG_ARCH_AARCH64)
    if ((pd[pd_idx] & (PAGE_PRESENT | PAGE_TABLE)) == PAGE_PRESENT)
#else
    if (pd[pd_idx] & PAGE_HUGE)
#endif
    {
        return (pd[pd_idx] & ~0x1FFFFFULL) + (virt_addr & 0x1FFFFF);
    }

    auto* pt = get_table(pd, pd_idx, false);
    if (!pt || !(pt[pt_idx] & PAGE_PRESENT)) return 0;

    return (pt[pt_idx] & ~0xFFFULL) + (virt_addr & 0xFFF);
#endif
}

uint64_t VMM::current_pml4() {
    return arch::read_cr3();
}

void VMM::map_page_in_pml4(uint64_t virt_addr, uint64_t phys_addr,
                            bool user, uint64_t pml4_phys)
{
#if defined(CONFIG_ARCH_RISCV64)
    // Sv39 3-level page table walk
    auto* l0 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        pml4_phys & ~0xFFFULL));

    size_t l0_idx = (virt_addr & VMM::L0_MASK) >> VMM::L0_SHIFT;
    size_t l1_idx = (virt_addr & VMM::L1_MASK) >> VMM::L1_SHIFT;
    size_t l2_idx = (virt_addr & VMM::L2_MASK) >> VMM::L2_SHIFT;

    auto* l1 = get_table(l0, l0_idx, true, true);
    auto* l2 = get_table(l1, l1_idx, true, true);

    // If L2 entry is a 2MB block (V=1, R|W|X=1), split it
    if ((l2[l2_idx] & PAGE_PRESENT) &&
        (l2[l2_idx] & (PAGE_READ | PAGE_WRITE | PAGE_EXEC))) {
        uint64_t new_l3_phys = PMM::alloc_user_page();
        ENSURE(new_l3_phys != 0);
        auto* new_l3 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + new_l3_phys);
        uint64_t block_base = l2[l2_idx] & ~0x1FFFFFULL;
        uint64_t base_flags = l2[l2_idx] & (PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_USER | PAGE_GLOBAL | PAGE_ACCESSED | PAGE_DIRTY);
        for (size_t i = 0; i < 512; ++i) {
            new_l3[i] = (block_base + i * 0x1000) | base_flags | PAGE_ACCESSED | PAGE_DIRTY;
        }
        // Table entry: V=1, no RWX
        l2[l2_idx] = new_l3_phys | PAGE_PRESENT;
    }

    auto* l3 = get_table(l2, l2_idx, true, true);

    uint64_t flags = PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    if (user) flags |= PAGE_USER;
    flags |= PAGE_ACCESSED | PAGE_DIRTY;

    l3[l2_idx] = phys_addr | flags;
#else
    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        pml4_phys & ~0xFFFULL));

    size_t pml4_idx = (virt_addr & PML4_MASK) >> PML4_SHIFT;
    size_t pdpt_idx = (virt_addr & PDPT_MASK) >> PDPT_SHIFT;
    size_t pd_idx   = (virt_addr & PD_MASK) >> PD_SHIFT;
    size_t pt_idx   = (virt_addr & PT_MASK) >> PT_SHIFT;

    auto* pdpt = get_table(pml4, pml4_idx, true, true);
    auto* pd   = get_table(pdpt, pdpt_idx, true, true);

#if defined(CONFIG_ARCH_AARCH64)
    if ((pd[pd_idx] & (PAGE_PRESENT | PAGE_TABLE)) == PAGE_PRESENT)
#else
    if (pd[pd_idx] & PAGE_HUGE)
#endif
    {
        uint64_t new_pt_phys = PMM::alloc_user_page();
        ENSURE(new_pt_phys != 0);
        auto* new_pt = reinterpret_cast<uint64_t*>(arch::
            HHDM_OFFSET + new_pt_phys);
        uint64_t huge_base  = pd[pd_idx] & ~0x1FFFFFULL;
#if defined(CONFIG_ARCH_AARCH64)
        uint64_t base_flags = pd[pd_idx] & (PAGE_PRESENT | PAGE_AF);
        for (size_t i = 0; i < 512; ++i) {
            new_pt[i] = (huge_base + i * 0x1000) | base_flags |
                PAGE_TABLE;
        }
        pd[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_TABLE;
#else
        uint64_t base_flags = pd[pd_idx] & (
            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        for (size_t i = 0; i < 512; ++i) {
            new_pt[i] = (huge_base + i * 0x1000) | base_flags;
        }
        pd[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
#endif
    }

    auto* pt   = get_table(pd, pd_idx, true, true);

#if defined(CONFIG_ARCH_AARCH64)
    uint64_t flags = PAGE_PRESENT | PAGE_TABLE | PAGE_AF;
    if (user) flags |= PAGE_AP_USER;
#else
    uint64_t flags = PAGE_PRESENT | PAGE_WRITE;
    if (user) flags |= PAGE_USER;
#endif

    pt[pt_idx] = phys_addr | flags;
#endif
}

uint64_t VMM::clone_kernel_pml4() {
    uint64_t phys = PMM::alloc_page();
    if (!phys) { ASSERT(errors::VmmError::VMM_ERR_PML4_ALLOC); return 0; }

#if defined(CONFIG_ARCH_RISCV64)
    auto* src = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        kernel_pml4_ & ~0xFFFULL));
    auto* dst = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + phys);

    // Clear user-space entries (L0 indices 0-255 for 0-256GB)
    for (size_t i = 0; i < 256; ++i) {
        dst[i] = 0;
    }
    // Copy kernel-space entries (L0 indices 256-511 for 256GB-512GB)
    for (size_t i = 256; i < 512; ++i) {
        dst[i] = src[i];
    }
    return phys;
#else
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
#endif
}

void VMM::free_user_pages(uint64_t pml4_phys) {
#if defined(CONFIG_ARCH_RISCV64)
    auto* l0 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        pml4_phys & ~0xFFFULL));
    // User space: L0 indices 0-255 (0-256GB)
    for (int l0_idx = 0; l0_idx < 256; ++l0_idx) {
        if (!(l0[l0_idx] & PAGE_PRESENT)) continue;
        uint64_t l1_phys = l0[l0_idx] & ~0xFFFULL;
        if (!PMM::is_user_page(l1_phys)) continue;
        auto* l1 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + l1_phys);
        for (int l1_idx = 0; l1_idx < 512; ++l1_idx) {
            if (!(l1[l1_idx] & PAGE_PRESENT)) continue;
            // Check for 2MB block at L1 level
            if ((l1[l1_idx] & (PAGE_READ | PAGE_WRITE | PAGE_EXEC))) {
                uint64_t page = l1[l1_idx] & ~0x1FFFFFULL;
                if (!PMM::is_user_page(page)) continue;
                PMM::free_page(page);
                continue;
            }
            uint64_t l2_phys = l1[l1_idx] & ~0xFFFULL;
            if (!PMM::is_user_page(l2_phys)) continue;
            auto* l2 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + l2_phys);
            for (int l2_idx = 0; l2_idx < 512; ++l2_idx) {
                if (!(l2[l2_idx] & PAGE_PRESENT)) continue;
                // Check for 2MB block at L2 level
                if ((l2[l2_idx] & (PAGE_READ | PAGE_WRITE | PAGE_EXEC))) {
                    uint64_t page = l2[l2_idx] & ~0x1FFFFFULL;
                    if (!PMM::is_user_page(page)) continue;
                    PMM::free_page(page);
                    continue;
                }
                uint64_t l3_phys = l2[l2_idx] & ~0xFFFULL;
                if (!PMM::is_user_page(l3_phys)) continue;
                auto* l3 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + l3_phys);
                for (int l3_idx = 0; l3_idx < 512; ++l3_idx) {
                    if (!(l3[l3_idx] & PAGE_PRESENT)) continue;
                    uint64_t leaf = l3[l3_idx] & ~0xFFFULL;
                    if (!PMM::is_user_page(leaf)) continue;
                    PMM::free_page(leaf);
                }
                PMM::free_page(l3_phys);
            }
            PMM::free_page(l2_phys);
        }
        PMM::free_page(l1_phys);
        l0[l0_idx] = 0;
    }
#else
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
#if defined(CONFIG_ARCH_AARCH64)
            if ((pdpt[pdpt_idx] & (PAGE_PRESENT | PAGE_TABLE)) == PAGE_PRESENT)
#else
            if (pdpt[pdpt_idx] & PAGE_HUGE)
#endif
            {
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
#if defined(CONFIG_ARCH_AARCH64)
                if ((pd[pd_idx] & (PAGE_PRESENT | PAGE_TABLE)) == PAGE_PRESENT)
#else
                if (pd[pd_idx] & PAGE_HUGE)
#endif
                {
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
#endif
    // TLB flush is the caller's responsibility (via CR3 reload)
}

uint64_t VMM::virt_to_phys_in_pml4(uint64_t virt_addr, uint64_t pml4_phys) {
#if defined(CONFIG_ARCH_RISCV64)
    auto* l0 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
        pml4_phys & ~0xFFFULL));

    size_t l0_idx = (virt_addr & VMM::L0_MASK) >> VMM::L0_SHIFT;
    size_t l1_idx = (virt_addr & VMM::L1_MASK) >> VMM::L1_SHIFT;
    size_t l2_idx = (virt_addr & VMM::L2_MASK) >> VMM::L2_SHIFT;

    if (!(l0[l0_idx] & PAGE_PRESENT)) return 0;
    auto* l1 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (l0[l0_idx] & ~0xFFFULL));

    if (!(l1[l1_idx] & PAGE_PRESENT)) return 0;
    auto* l2 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (l1[l1_idx] & ~0xFFFULL));

    // Check for 2MB block mapping (leaf at L1 level)
    if ((l2[l1_idx] & PAGE_PRESENT) && (l2[l1_idx] & (PAGE_READ | PAGE_WRITE | PAGE_EXEC))) {
        return (l2[l1_idx] & ~0x1FFFFFULL) + (virt_addr & 0x1FFFFF);
    }

    if (!(l2[l2_idx] & PAGE_PRESENT)) return 0;
    auto* l3 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (l2[l2_idx] & ~0xFFFULL));

    if (!(l3[l2_idx] & PAGE_PRESENT)) return 0;
    return (l3[l2_idx] & ~0xFFFULL) + (virt_addr & 0xFFF);
#else
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

#if defined(CONFIG_ARCH_AARCH64)
    if ((pd[pd_idx] & (PAGE_PRESENT | PAGE_TABLE)) == PAGE_PRESENT)
#else
    if (pd[pd_idx] & PAGE_HUGE)
#endif
    {
        return (pd[pd_idx] & ~0x1FFFFFULL) + (virt_addr & 0x1FFFFF);
    }

    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    auto* pt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pd[pd_idx
        ] & ~0xFFFULL));

    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFFULL) + (virt_addr & 0xFFF);
#endif
}

} // namespace kernel
