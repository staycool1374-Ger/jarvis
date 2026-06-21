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

/// @file vmm.hpp
/// @brief Virtual Memory Manager — x86-64 page table management.

#pragma once

#include <types.hpp>
#include <constants.hpp>

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
    static void map_page(uint64_t virt_addr, uint64_t phys_addr,
        bool user = false);
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

    /// @brief Creates a fresh PML4: zeroes user entries (0-255),
    /// copies kernel entries (256-511). Used during exec/load —
    /// NOT for fork (which needs parent user entries).
    /// @return Physical address of the new PML4, or 0 on failure.
    static uint64_t clone_kernel_pml4();

    /// @brief Maps a page into a specific page table (not the kernel one).
    /// @param virt_addr Virtual address (page-aligned).
    /// @param phys_addr Physical address (page-aligned).
    /// @param user      If true, sets user-accessible flag.
    /// @param pml4_phys Physical address of the target PML4.
    static void map_page_in_pml4(uint64_t virt_addr, uint64_t phys_addr,
                                 bool user, uint64_t pml4_phys);

    /// @brief Returns the physical address of the kernel PML4.
    static uint64_t get_kernel_pml4() { return kernel_pml4_; }

    /// @brief Frees all user-space pages and page tables from a
    /// user PML4. Used during exec() to clean up the old address
    /// space. Skips kernel-owned pages (HHDM-mapped) and honours
    /// page_table_shared_ flag.
    /// @param pml4_phys Physical address of the user PML4.
    static void free_user_pages(uint64_t pml4_phys);

    /// @brief Translates a virtual address to a physical address
    /// using a specific PML4.
    /// @param virt_addr Virtual address to translate.
    /// @param pml4_phys Physical address of the target PML4.
    /// @return Physical address, or 0 if not mapped.
    static uint64_t virt_to_phys_in_pml4(uint64_t virt_addr,
                                         uint64_t pml4_phys);

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
    /// @param user_alloc If true, allocate page table page as USER-owned.
    /// @return Pointer to the next-level table, or nullptr.
    static uint64_t* get_table(uint64_t* table, size_t index, bool create,
        bool user_alloc = false);
};

} // namespace kernel
