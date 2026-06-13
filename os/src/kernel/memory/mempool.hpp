/// @file mempool.hpp
/// @brief Fixed-size block memory pool allocator (k malloc).

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Fixed-size block memory allocator with multiple pool classes.
/// @note Provides O(1) allocation/free for kernel heap use.
class MemPool {
public:
    static constexpr size_t POOL_COUNT = 8;

    /// @brief Describes a single pool of fixed-size blocks.
    /// @note Free-list is embedded in the data pages (each free block's first 8 bytes store the next index).
    struct Pool {
        size_t  block_size;
        size_t  block_count;
        size_t  free_count;
        uint8_t* data;
        size_t  first_free;
        bool    initialized;

        Pool()
            : block_size(0)
            , block_count(0)
            , free_count(0)
            , data(nullptr)
            , first_free(0)
            , initialized(false)
            {}
    };

    /// @brief Initialises all pool classes from pre-allocated memory.
    static void init();
    /// @brief Allocates a block of at least the requested size.
    /// @param size Minimum number of bytes.
    /// @return Pointer to the allocated block, or nullptr.
    static void* alloc(size_t size);
    /// @brief Frees a block previously returned by alloc().
    /// @param block Pointer to the block to free.
    static void free(void* block);

private:
    static Pool pools_[POOL_COUNT];

    /// @brief Finds the smallest pool class that satisfies a given size.
    /// @param size Requested allocation size.
    /// @return Pool index, or POOL_COUNT if too large.
    static size_t find_pool(size_t size);
};

} // namespace kernel
