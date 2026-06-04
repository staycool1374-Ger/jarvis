/// @file pmm.hpp
/// @brief Physical Memory Manager — bitmap-based page allocator.

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Physical memory manager using a bitmap to track free 4 KiB pages.
/// @note All physical page allocation and deallocation goes through PMM.
class PMM {
public:
    /// @brief Initialises the PMM with the available memory region.
    /// @param mem_size     Total physical memory size in bytes.
    /// @param kernel_start Physical address where the kernel begins.
    /// @param kernel_end   Physical address where the kernel ends.
    static void init(uint64_t mem_size, uint64_t kernel_start, uint64_t kernel_end);

    /// @brief Allocates a single 4 KiB physical page.
    /// @return Physical address of the allocated page, or 0 on failure.
    static uint64_t alloc_page();
    /// @brief Allocates a contiguous block of physical pages.
    /// @param count Number of consecutive 4 KiB pages.
    /// @return Physical base address, or 0 on failure.
    static uint64_t alloc_contiguous(size_t count);
    /// @brief Frees a previously allocated physical page.
    /// @param phys_addr Physical address of the page to free.
    static void free_page(uint64_t phys_addr);

    /// @brief Returns the amount of free physical memory in bytes.
    /// @return Free memory in bytes.
    static uint64_t free_memory() noexcept { return free_pages_ * 4096; }
    /// @brief Returns the total physical memory managed in bytes.
    /// @return Total memory in bytes.
    static uint64_t total_memory() noexcept { return total_pages_ * 4096; }

private:
    static constexpr uint64_t PAGE_SIZE = 4096;

    static uint64_t total_pages_;
    static uint64_t free_pages_;
    static uint64_t bitmap_;
    static uint64_t bitmap_size_;

    /// @brief Marks a page as allocated in the bitmap.
    /// @param index Page index in the bitmap.
    static void bitmap_set(size_t index);
    /// @brief Marks a page as free in the bitmap.
    /// @param index Page index in the bitmap.
    static void bitmap_clear(size_t index);
    /// @brief Tests whether a page is allocated.
    /// @param index Page index in the bitmap.
    /// @return True if the page is in use.
    static bool bitmap_test(size_t index);
};

} // namespace kernel
