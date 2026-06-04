/// @file vmm.hpp
/// @brief Virtual Memory Manager — x86-64 page table management.

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Virtual memory manager for x86-64 4-level paging.
/// @note Maps virtual to physical pages; supports user/kernel page flags.
class VMM {
public:
    /// @brief Initialises the VMM and sets up the kernel page tables.
    static void init();

    /// @brief Maps a virtual page to a physical page.
    /// @param virt_addr Virtual address (page-aligned).
    /// @param phys_addr Physical address (page-aligned).
    /// @param user      If true, sets the user-accessible flag.
    static void map_page(uint64_t virt_addr, uint64_t phys_addr, bool user = false);
    /// @brief Unmaps a virtual page.
    /// @param virt_addr Virtual address to unmap.
    static void unmap_page(uint64_t virt_addr);
    /// @brief Translates a virtual address to its physical address.
    /// @param virt_addr Virtual address.
    /// @return Physical address, or 0 if not mapped.
    static uint64_t virt_to_phys(uint64_t virt_addr);

    /// @brief Returns the physical address of the current PML4 table.
    /// @return Physical address of PML4.
    static uint64_t current_pml4();

private:
    static constexpr uint64_t PAGE_SIZE = 4096;
    static constexpr uint64_t PAGE_TABLE_ENTRIES = 512;
    static constexpr uint64_t PAGE_PRESENT  = 1ULL << 0;
    static constexpr uint64_t PAGE_WRITE    = 1ULL << 1;
    static constexpr uint64_t PAGE_USER     = 1ULL << 2;
    static constexpr uint64_t PAGE_HUGE     = 1ULL << 7;

    static constexpr uint64_t PML4_SHIFT    = 39;
    static constexpr uint64_t PDPT_SHIFT    = 30;
    static constexpr uint64_t PD_SHIFT      = 21;
    static constexpr uint64_t PT_SHIFT      = 12;

    static constexpr uint64_t PML4_MASK     = 0x1FFULL << PML4_SHIFT;
    static constexpr uint64_t PDPT_MASK     = 0x1FFULL << PDPT_SHIFT;
    static constexpr uint64_t PD_MASK       = 0x1FFULL << PD_SHIFT;
    static constexpr uint64_t PT_MASK       = 0x1FFULL << PT_SHIFT;

    static uint64_t kernel_pml4_;

    /// @brief Walks or creates a page-table entry at the given level.
    /// @param table Pointer to the current-level page table.
    /// @param index Index into the table.
    /// @param create If true, allocates a new table if missing.
    /// @return Pointer to the next-level table, or nullptr.
    static uint64_t* get_table(uint64_t* table, size_t index, bool create);
};

} // namespace kernel
