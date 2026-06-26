#pragma once

#include <types.hpp>
#include <kernel/arch/hal/io.hpp>
#include <kernel/arch/hal/page_flags.hpp>
#include <lib/error.hpp>
#include <lib/string.hpp>
#include <kernel/memory/pmm.hpp>

namespace arch {

// Sv39: 3-level page table (512 entries each)
// L0 -> 1GB regions (bits 38:30)
// L1 -> 2MB regions (bits 29:21)
// L2 -> 4KB pages   (bits 20:12)

inline uint64_t arch_page_table_current() {
    uint64_t satp;
    asm volatile("csrr %0, satp" : "=r"(satp) : : "memory");
    return (satp & 0xFFFFFFFFFFF) << 12; // PPN -> physical address
}

inline void arch_page_table_activate(uint64_t phys_ppn_shifted) {
    // Input is physical address >> 12 (PPN)
    uint64_t satp = (8ULL << 60) | (phys_ppn_shifted >> 12); // Sv39 mode | PPN
    asm volatile("csrw satp, %0" : : "r"(satp) : "memory");
    asm volatile("sfence.vma" : : : "memory");
}

inline void arch_page_table_tlb_flush(uint64_t virt_addr) {
    asm volatile("sfence.vma %0" : : "r"(virt_addr) : "memory");
}

inline void arch_page_table_tlb_flush_all() {
    asm volatile("sfence.vma" : : : "memory");
}

class ArchPageTable {
public:
    static inline uint64_t current() { return arch_page_table_current(); }
    static inline void activate(uint64_t phys) { arch_page_table_activate(phys); }
    static inline void tlb_flush(uint64_t virt_addr) { arch_page_table_tlb_flush(virt_addr); }
    static inline void tlb_flush_all() { arch_page_table_tlb_flush_all(); }

    static constexpr uint64_t PAGE_SIZE = CONFIG_PAGE_SIZE;
    static constexpr uint64_t ENTRIES = 512;

    static kernel::ErrorOr<void> map_page(uint64_t virt, uint64_t phys, uint64_t flags);
    static kernel::ErrorOr<void> unmap_page(uint64_t virt);
    static uint64_t get_physical(uint64_t virt);

private:
    // Sv39: 3 levels
    static constexpr uint64_t L0_SHIFT = 30;
    static constexpr uint64_t L1_SHIFT = 21;
    static constexpr uint64_t L2_SHIFT = 12;
    static constexpr uint64_t TABLE_MASK = 0x1FF;
    static constexpr uint64_t DESC_VALID = 1ULL << 0;
    static constexpr uint64_t DESC_TABLE = 1ULL << 1; // points to next level
    static constexpr uint64_t DESC_PAGE = 1ULL << 1;  // 4KB page (L2 leaf)
    static constexpr uint64_t DESC_BLOCK = 0ULL;       // 2MB/1GB block
    static constexpr uint64_t DESC_AF = 1ULL << 6;     // Accessed flag
    static constexpr uint64_t DESC_G = 1ULL << 5;      // Global mapping
    static constexpr uint64_t DESC_USER = 1ULL << 4;   // User-accessible
    static constexpr uint64_t DESC_R = 1ULL << 1;      // Readable
    static constexpr uint64_t DESC_W = 1ULL << 2;      // Writable
    static constexpr uint64_t DESC_X = 1ULL << 3;      // Executable
    static constexpr uint64_t DESC_A = 1ULL << 6;      // Accessed (same as AF on some impls)
    static constexpr uint64_t DESC_D = 1ULL << 7;      // Dirty

    static uint64_t* get_table(uint64_t table_base, uint64_t index, bool create);
    static uint64_t attr_from_flags(uint64_t flags);
    static uint64_t* walk_l0(uint64_t l0_base, size_t l0_idx, bool create);
    static uint64_t* walk_l1(uint64_t l1_base, size_t l1_idx, bool create);
    static uint64_t* walk_l2(uint64_t l2_base, size_t l2_idx, bool create);
};

inline uint64_t* ArchPageTable::get_table(uint64_t table_base, uint64_t index, bool create) {
    uint64_t* table = reinterpret_cast<uint64_t*>(table_base);
    uint64_t entry = table[index];

    if (entry & DESC_VALID) {
        return reinterpret_cast<uint64_t*>(entry & ~0xFFF);
    }

    if (!create) return nullptr;

    uint64_t new_page = kernel::PMM::alloc_page_table();
    if (!new_page) return nullptr;

    void* new_page_ptr = reinterpret_cast<void*>(new_page);
    memset(new_page_ptr, 0, PAGE_SIZE);
    // Mark as valid, readable, writable table pointer
    table[index] = (new_page & ~0xFFF) | DESC_VALID | DESC_TABLE | DESC_R | DESC_W;
    return reinterpret_cast<uint64_t*>(new_page);
}

inline uint64_t ArchPageTable::attr_from_flags(uint64_t flags) {
    uint64_t attr = DESC_VALID | DESC_AF;
    (void)DESC_G;
    if (flags & kernel::PageFlags::PRESENT) attr |= DESC_VALID;
    if (!(flags & kernel::PageFlags::NX)) attr |= DESC_X;
    if (flags & kernel::PageFlags::WRITE) attr |= DESC_W;
    if (flags & kernel::PageFlags::USER) attr |= DESC_USER;
    // Default to readable unless explicitly no-access
    if (flags & kernel::PageFlags::PRESENT) attr |= DESC_R;
    return attr;
}

inline uint64_t* ArchPageTable::walk_l0(uint64_t l0_base, size_t l0_idx, bool create) {
    return get_table(l0_base, l0_idx, create);
}

inline uint64_t* ArchPageTable::walk_l1(uint64_t l1_base, size_t l1_idx, bool create) {
    return get_table(l1_base, l1_idx, create);
}

inline uint64_t* ArchPageTable::walk_l2(uint64_t l2_base, size_t l2_idx, bool create) {
    return get_table(l2_base, l2_idx, create);
}

inline kernel::ErrorOr<void> ArchPageTable::map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t l0_idx = (virt >> L0_SHIFT) & TABLE_MASK;
    uint64_t l1_idx = (virt >> L1_SHIFT) & TABLE_MASK;
    uint64_t l2_idx = (virt >> L2_SHIFT) & TABLE_MASK;

    uint64_t l0_phys = current();
    uint64_t* l1 = walk_l0(l0_phys, l0_idx, true);
    if (!l1) return kernel::Error::OOM;

    uint64_t* l2 = walk_l1(reinterpret_cast<uint64_t>(l1), l1_idx, true);
    if (!l2) return kernel::Error::OOM;

    uint64_t entry_flags = attr_from_flags(flags);

    // Try 2MB block mapping if aligned
    if ((virt & ((1ULL << L1_SHIFT) - 1)) == 0 && (phys & ((1ULL << L1_SHIFT) - 1)) == 0) {
        l2[l1_idx] = (phys & ~0x1FFFFF) | entry_flags | DESC_BLOCK | (1ULL << 6);
        return {};
    }

    // 4KB page at L2
    l2[l2_idx] = (phys & ~0xFFF) | entry_flags | DESC_PAGE | (1ULL << 6) | (1ULL << 7);
    return {};
}

inline kernel::ErrorOr<void> ArchPageTable::unmap_page(uint64_t virt) {
    uint64_t l0_idx = (virt >> L0_SHIFT) & TABLE_MASK;
    uint64_t l1_idx = (virt >> L1_SHIFT) & TABLE_MASK;
    uint64_t l2_idx = (virt >> L2_SHIFT) & TABLE_MASK;

    uint64_t l0_phys = current();
    uint64_t* l0 = reinterpret_cast<uint64_t*>(l0_phys);

    if (!(l0[l0_idx] & DESC_VALID)) return kernel::Error::NOT_FOUND;
    uint64_t* l1 = reinterpret_cast<uint64_t*>(l0[l0_idx] & ~0xFFF);
    if (!(l1[l1_idx] & DESC_VALID)) return kernel::Error::NOT_FOUND;
    uint64_t* l2 = reinterpret_cast<uint64_t*>(l1[l1_idx] & ~0xFFF);

    // Check for 2MB block
    if ((virt & ((1ULL << L1_SHIFT) - 1)) == 0 && !(l2[l1_idx] & DESC_TABLE)) {
        l2[l1_idx] = 0;
        return {};
    }

    if (!(l2[l2_idx] & DESC_VALID)) return kernel::Error::NOT_FOUND;
    l2[l2_idx] = 0;
    return {};
}

inline uint64_t ArchPageTable::get_physical(uint64_t virt) {
    uint64_t l0_idx = (virt >> L0_SHIFT) & TABLE_MASK;
    uint64_t l1_idx = (virt >> L1_SHIFT) & TABLE_MASK;
    uint64_t l2_idx = (virt >> L2_SHIFT) & TABLE_MASK;

    uint64_t l0_phys = current();
    uint64_t* l0 = reinterpret_cast<uint64_t*>(l0_phys);

    if (!(l0[l0_idx] & DESC_VALID)) return 0;
    uint64_t* l1 = reinterpret_cast<uint64_t*>(l0[l0_idx] & ~0xFFF);
    if (!(l1[l1_idx] & DESC_VALID)) return 0;
    uint64_t* l2 = reinterpret_cast<uint64_t*>(l1[l1_idx] & ~0xFFF);

    // 2MB block mapping
    if ((virt & ((1ULL << L1_SHIFT) - 1)) == 0 && !(l2[l1_idx] & DESC_TABLE)) {
        return (l2[l1_idx] & ~0x1FFFFF) | (virt & 0x1FFFFF);
    }

    if (!(l2[l2_idx] & DESC_VALID)) return 0;
    return (l2[l2_idx] & ~0xFFF) | (virt & 0xFFF);
}

} // namespace arch
