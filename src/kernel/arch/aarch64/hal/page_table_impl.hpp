#pragma once

#include <types.hpp>
#include <kernel/arch/hal/io.hpp>
#include <kernel/arch/hal/page_flags.hpp>
#include <lib/error.hpp>
#include <lib/string.hpp>
#include <kernel/memory/pmm.hpp>

namespace arch {

inline uint64_t arch_page_table_current() { return read_ttbr1_el1(); }
inline void arch_page_table_activate(uint64_t ttbr1_phys) {
    write_ttbr1_el1(ttbr1_phys);
    dsb_sy();
    isb();
}
inline void arch_page_table_tlb_flush(uint64_t virt_addr) {
    tlbi_vae1(virt_addr);
    dsb_sy();
    isb();
}
inline void arch_page_table_tlb_flush_all() {
    tlbi_alle1();
    dsb_sy();
    isb();
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
    static constexpr uint64_t L0_SHIFT = 39;
    static constexpr uint64_t L1_SHIFT = 30;
    static constexpr uint64_t L2_SHIFT = 21;
    static constexpr uint64_t L3_SHIFT = 12;
    static constexpr uint64_t TABLE_MASK = 0x1FF;
    static constexpr uint64_t DESC_VALID = 1ULL << 0;
    static constexpr uint64_t DESC_TABLE = 1ULL << 1;
    static constexpr uint64_t DESC_BLOCK = 0ULL;
    static constexpr uint64_t DESC_AF = 1ULL << 10;
    static constexpr uint64_t DESC_SH_INNER = 3ULL << 8;
    static constexpr uint64_t DESC_NS = 1ULL << 5;
    static constexpr uint64_t ATTR_IDX_NORMAL = 1ULL << 2;
    static constexpr uint64_t ATTR_IDX_DEVICE = 0ULL;
    static constexpr uint64_t AP_RO = 2ULL << 6;
    static constexpr uint64_t AP_RW = 0ULL;
    static constexpr uint64_t UXN = 1ULL << 54;
    static constexpr uint64_t PXN = 1ULL << 53;

    static uint64_t* get_table(uint64_t table_base, uint64_t index, bool create);
    static uint64_t attr_from_flags(uint64_t flags);
    static uint64_t* walk_l0(uint64_t l0_base, size_t l0_idx, bool create);
    static uint64_t* walk_l1(uint64_t l1_base, size_t l1_idx, bool create);
    static uint64_t* walk_l2(uint64_t l2_base, size_t l2_idx, bool create);
    static uint64_t* walk_l3(uint64_t l3_base, size_t l3_idx, bool create);
};

inline uint64_t* ArchPageTable::get_table(uint64_t table_base, uint64_t index, bool create) {
    uint64_t* table = reinterpret_cast<uint64_t*>(table_base);
    uint64_t entry = table[index];
    
    if (entry & DESC_VALID) {
        if (entry & DESC_TABLE) {
            return reinterpret_cast<uint64_t*>(entry & ~0xFFF);
        }
        return nullptr;
    }
    
    if (!create) return nullptr;
    
    uint64_t new_page = kernel::PMM::alloc_page_table();
    if (!new_page) return nullptr;
    
    void* new_page_ptr = reinterpret_cast<void*>(new_page);
    memset(new_page_ptr, 0, PAGE_SIZE);
    table[index] = new_page | DESC_VALID | DESC_TABLE;
    return reinterpret_cast<uint64_t*>(new_page);
}

inline uint64_t ArchPageTable::attr_from_flags(uint64_t flags) {
    uint64_t attr = DESC_VALID | DESC_AF | DESC_SH_INNER | DESC_NS;
    if (flags & kernel::PageFlags::PRESENT) attr |= DESC_VALID;
    if (flags & kernel::PageFlags::WRITE) attr |= AP_RW;
    else attr |= AP_RO;
    if (flags & kernel::PageFlags::USER) {
        // User accessible
    } else {
        attr |= UXN | PXN;
    }
    if (flags & kernel::PageFlags::NX) attr |= UXN | PXN;
    if (flags & kernel::PageFlags::CACHE_DISABLED) attr |= ATTR_IDX_DEVICE;
    else attr |= ATTR_IDX_NORMAL;
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

inline uint64_t* ArchPageTable::walk_l3(uint64_t l3_base, size_t l3_idx, bool create) {
    return get_table(l3_base, l3_idx, create);
}

inline kernel::ErrorOr<void> ArchPageTable::map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t l0_idx = (virt >> L0_SHIFT) & TABLE_MASK;
    uint64_t l1_idx = (virt >> L1_SHIFT) & TABLE_MASK;
    uint64_t l2_idx = (virt >> L2_SHIFT) & TABLE_MASK;
    uint64_t l3_idx = (virt >> L3_SHIFT) & TABLE_MASK;
    
    uint64_t l0_phys = current();
    
    uint64_t* l1 = walk_l0(l0_phys, l0_idx, true);
    if (!l1) return kernel::Error::OOM;
    
    uint64_t* l2 = walk_l1(reinterpret_cast<uint64_t>(l1), l1_idx, true);
    if (!l2) return kernel::Error::OOM;
    
    uint64_t entry_flags = attr_from_flags(flags);
    
    if ((virt & ((1ULL << L2_SHIFT) - 1)) == 0 && (phys & ((1ULL << L2_SHIFT) - 1)) == 0) {
        l2[l2_idx] = phys | entry_flags | DESC_BLOCK | (1ULL << 10);
        dsb_sy();
        isb();
        return {};
    }
    
    uint64_t* l3 = walk_l2(reinterpret_cast<uint64_t>(l2), l2_idx, true);
    if (!l3) return kernel::Error::OOM;
    
    l3[l3_idx] = phys | entry_flags | DESC_BLOCK | (1ULL << 10);
    dsb_sy();
    isb();
    return {};
}

inline kernel::ErrorOr<void> ArchPageTable::unmap_page(uint64_t virt) {
    uint64_t l0_idx = (virt >> L0_SHIFT) & TABLE_MASK;
    uint64_t l1_idx = (virt >> L1_SHIFT) & TABLE_MASK;
    uint64_t l2_idx = (virt >> L2_SHIFT) & TABLE_MASK;
    uint64_t l3_idx = (virt >> L3_SHIFT) & TABLE_MASK;
    
    uint64_t l0_phys = current();
    uint64_t* l0 = reinterpret_cast<uint64_t*>(l0_phys);
    
    if (!(l0[l0_idx] & DESC_VALID)) return kernel::Error::NOT_FOUND;
    uint64_t* l1 = reinterpret_cast<uint64_t*>(l0[l0_idx] & ~0xFFF);
    if (!(l1[l1_idx] & DESC_VALID)) return kernel::Error::NOT_FOUND;
    uint64_t* l2 = reinterpret_cast<uint64_t*>(l1[l1_idx] & ~0xFFF);
    
    if ((virt & ((1ULL << L2_SHIFT) - 1)) == 0 && (l2[l2_idx] & DESC_BLOCK)) {
        l2[l2_idx] = 0;
        dsb_sy();
        isb();
        return {};
    }
    
    if (!(l2[l2_idx] & DESC_VALID)) return kernel::Error::NOT_FOUND;
    uint64_t* l3 = reinterpret_cast<uint64_t*>(l2[l2_idx] & ~0xFFF);
    
    if (!(l3[l3_idx] & DESC_VALID)) return kernel::Error::NOT_FOUND;
    l3[l3_idx] = 0;
    dsb_sy();
    isb();
    return {};
}

inline uint64_t ArchPageTable::get_physical(uint64_t virt) {
    uint64_t l0_idx = (virt >> L0_SHIFT) & TABLE_MASK;
    uint64_t l1_idx = (virt >> L1_SHIFT) & TABLE_MASK;
    uint64_t l2_idx = (virt >> L2_SHIFT) & TABLE_MASK;
    uint64_t l3_idx = (virt >> L3_SHIFT) & TABLE_MASK;
    
    uint64_t l0_phys = current();
    uint64_t* l0 = reinterpret_cast<uint64_t*>(l0_phys);
    
    if (!(l0[l0_idx] & DESC_VALID)) return 0;
    uint64_t* l1 = reinterpret_cast<uint64_t*>(l0[l0_idx] & ~0xFFF);
    if (!(l1[l1_idx] & DESC_VALID)) return 0;
    uint64_t* l2 = reinterpret_cast<uint64_t*>(l1[l1_idx] & ~0xFFF);
    
    if ((virt & ((1ULL << L2_SHIFT) - 1)) == 0 && (l2[l2_idx] & DESC_BLOCK)) {
        return (l2[l2_idx] & ~0x1FFFFF) | (virt & 0x1FFFFF);
    }
    
    if (!(l2[l2_idx] & DESC_VALID)) return 0;
    uint64_t* l3 = reinterpret_cast<uint64_t*>(l2[l2_idx] & ~0xFFF);
    if (!(l3[l3_idx] & DESC_VALID)) return 0;
    
    return (l3[l3_idx] & ~0xFFF) | (virt & 0xFFF);
}

} // namespace arch
