/// @file pmm.hpp
/// @brief Physical Memory Manager — bitmap-based page allocator with owner tracking.

#pragma once

#include <types.hpp>
#include <constants.hpp>

namespace kernel {

/// @brief Physical memory manager using a bitmap to track free 4 KiB pages.
/// @note All physical page allocation and deallocation goes through PMM.
///       Tracks USER vs KERNEL page ownership for safety in free_user_pages.
class PMM {
public:
    static void init(uint64_t mem_size, uint64_t kernel_start, uint64_t kernel_end);

    /// @brief Allocates a single 4 KiB page (KERNEL ownership).
    static uint64_t alloc_page();
    /// @brief Allocates a contiguous block of pages (KERNEL ownership).
    static uint64_t alloc_contiguous(size_t count);
    /// @brief Allocates a single 4 KiB page (USER ownership).
    static uint64_t alloc_user_page();
    /// @brief Allocates contiguous pages (USER ownership).
    static uint64_t alloc_user_contiguous(size_t count);
    /// @brief Allocates a single 4 KiB page for page tables (KERNEL ownership, from reserved low-memory pool).
    static uint64_t alloc_page_table();
    /// @brief Frees a page regardless of ownership.
    static void free_page(uint64_t phys_addr);

    /// @brief Returns true if the page was allocated as USER ownership.
    static bool is_user_page(uint64_t phys_addr);

    static uint64_t free_memory() noexcept { return free_pages_ * arch::PAGE_SIZE; }
    static uint64_t total_memory() noexcept { return total_pages_ * arch::PAGE_SIZE; }

    /// @brief OOM handler type — called when allocation fails. Should try to free memory.
    /// @return true if memory may have been freed (caller should retry).
    using OOMHandler = bool (*)();
    static void set_oom_handler(OOMHandler h) { oom_handler_ = h; }

private:
    static constexpr uint64_t PAGE_SIZE = 4096;

    static uint64_t total_pages_;
    static uint64_t free_pages_;
    static uint64_t bitmap_;
    static uint64_t bitmap_size_;
    static uint64_t owner_bitmap_;
    static uint64_t page_table_pool_start_;
    static uint64_t page_table_pool_end_;
    static OOMHandler oom_handler_;

    static void bitmap_set(size_t index);
    static void bitmap_clear(size_t index);
    static bool bitmap_test(size_t index);

    static void owner_set_user(size_t index);
    static void owner_set_kernel(size_t index);
    static bool owner_test(size_t index);

    static uint64_t try_alloc_user(size_t count);
    static uint64_t try_alloc_kernel(size_t count);
};

} // namespace kernel
