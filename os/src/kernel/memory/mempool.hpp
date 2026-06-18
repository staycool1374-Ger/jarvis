/// @file mempool.hpp
/// @brief Fixed-size block memory pool allocator (k malloc).

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Fixed-size block memory allocator with multiple pool classes.
/// @note Provides O(1) allocation/free for kernel heap use.
class MemPool {
public:
    static constexpr size_t POOL_COUNT = 9;

    /// @brief Describes a single pool of fixed-size blocks.
    /// @note Free-list is embedded in the data pages (each free
    /// block's first 8 bytes store the next index).
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

        bool is_block_freed(size_t idx) const {
            return freed_bitmap[idx / 64] & (1ULL << (idx % 64));
        }

        void set_block_freed(size_t idx) {
            freed_bitmap[idx / 64] |= (1ULL << (idx % 64));
        }

        void clear_block_freed(size_t idx) {
            freed_bitmap[idx / 64] &= ~(1ULL << (idx % 64));
        }

        /// @name Test-isolation helpers
        void copy_freed_bitmap(uint64_t* dst) const {
            for (int i = 0; i < 4; ++i) dst[i] = freed_bitmap[i];
        }
        void write_freed_bitmap(const uint64_t* src) {
            for (int i = 0; i < 4; ++i) freed_bitmap[i] = src[i];
        }

    private:
        uint64_t freed_bitmap[4];  // 256 bits — covers all pools
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

    /// @name Test-isolation helpers (snapshot / restore)
    struct PoolMeta {
        size_t  first_free;
        size_t  free_count;
        uint64_t freed_bitmap[4];
    };
    /// @brief Return the number of pool classes.
    static size_t pool_count() { return POOL_COUNT; }
    /// @brief Return free count for pool at index @p idx.
    static size_t pool_free_count(size_t idx) { return pools_[idx].free_count; }
    /// @brief Fill @p out with the meta of pool @p idx.
    static void capture_pool_meta(size_t idx, PoolMeta& out);
    /// @brief Restore pool @p idx from @p meta and rebuild its free list.
    static void restore_pool_meta(size_t idx, const PoolMeta& meta);
    /// @brief Write all pool block-data into @p dst (size = pool_data_bytes()).
    static void capture_pool_data(uint8_t* dst);
    /// @brief Overwrite all pool block-data from @p src.
    static void restore_pool_data(const uint8_t* src);
    /// @brief Total bytes needed to hold all pool block-data copies.
    static size_t pool_data_bytes();

private:
    static Pool pools_[POOL_COUNT];

    /// @brief Finds the smallest pool class that satisfies a given size.
    /// @param size Requested allocation size.
    /// @return Pool index, or POOL_COUNT if too large.
    static size_t find_pool(size_t size);
};

} // namespace kernel
