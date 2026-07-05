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

/// @file mempool.hpp
/// @brief Fixed-size block memory pool allocator (k malloc).  O(1) alloc/free
///        via embedded free-list per pool class (9 classes, 16–4480 bytes).

#pragma once

#include <types.hpp>
#include <kernel/memory/mempool_errors.hpp>

namespace kernel {

/// @brief Fixed-size block memory allocator with 9 pool classes.
///
/// Each pool is a contiguous page-aligned region partitioned into fixed-size
/// blocks.  Free blocks are linked via an embedded singly-linked free-list
/// (the first 8 bytes of each free block store the next index).
///
/// @note O(1) allocation: find_pool (linear scan of 9 classes) + pop from
///       free-list head.  O(1) free: linear scan of 9 pools to find owner
///       + push to free-list head.  No bitmap scans or dynamic allocation.
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

        /// @brief Default constructor — zero-initialises all fields.
        Pool()
            : block_size(0)
            , block_count(0)
            , free_count(0)
            , data(nullptr)
            , first_free(0)
            , initialized(false)
            {}

        /// @brief Check if a block index is marked as freed in the bitmap.
        /// @param idx Block index.
        /// @return true if the block is free.
        bool is_block_freed(size_t idx) const {
            return freed_bitmap[idx / 64] & (1ULL << (idx % 64));
        }

        /// @brief Mark a block index as freed in the bitmap.
        /// @param idx Block index.
        void set_block_freed(size_t idx) {
            freed_bitmap[idx / 64] |= (1ULL << (idx % 64));
        }

        /// @brief Clear the freed flag for a block index (mark as allocated).
        /// @param idx Block index.
        void clear_block_freed(size_t idx) {
            freed_bitmap[idx / 64] &= ~(1ULL << (idx % 64));
        }

        /// @brief Copy the freed bitmap to an external buffer.
        /// @param[out] dst Destination array (4 x uint64_t).
        void copy_freed_bitmap(uint64_t* dst) const {
            for (int i = 0; i < 4; ++i) dst[i] = freed_bitmap[i];
        }
        /// @brief Overwrite the freed bitmap from an external buffer.
        /// @param src Source array (4 x uint64_t).
        void write_freed_bitmap(const uint64_t* src) {
            for (int i = 0; i < 4; ++i) freed_bitmap[i] = src[i];
        }

    private:
        uint64_t freed_bitmap[4];  // 256 bits — covers all pools
    };

    /// @brief Initialises all pool classes from pre-allocated memory.
    static void init();
    /// @brief Initialises with error code.
    /// @return MemPoolError code.
    static errors::MemPoolError init_err();

    /// @brief Allocates a block of at least the requested size.
    /// @param size Minimum number of bytes.
    /// @return Pointer to the allocated block, or nullptr.
    static void* alloc(size_t size);
    /// @brief Allocates a block with error code.
    /// @param size Minimum number of bytes.
    /// @param[out] out_ptr Pointer to allocated block on success.
    /// @return MemPoolError code.
    static errors::MemPoolError alloc_err(size_t size, void*& out_ptr);

    /// @brief Frees a block previously returned by alloc().
    /// @param block Pointer to the block to free.
    static void free(void* block);
    /// @brief Frees a block with error code.
    /// @param block Pointer to the block to free.
    /// @return MemPoolError code.
    static errors::MemPoolError free_err(void* block);

    /// @brief Check if a pointer falls within any MemPool pool range.
    /// @param ptr Pointer to check.
    /// @return true if ptr is owned by MemPool.
    static bool contains(void* ptr);

    /// @brief Check whether the MemPool subsystem has been initialised.
    /// @return true after init() completes successfully.
    static bool is_ready() { return ready_; }

    /// @name Test-isolation helpers (snapshot / restore)
    struct PoolMeta {
        size_t  first_free;
        size_t  free_count;
        size_t  block_count;
        uint64_t freed_bitmap[4];
    };
    /// @brief Return the number of pool classes.
    static size_t pool_count() { return POOL_COUNT; }
    /// @brief Return the free block count for a given pool.
    /// @param idx Pool index (0..POOL_COUNT-1).
    /// @return Number of free blocks in that pool.
    static size_t pool_free_count(size_t idx) { return pools_[idx].free_count; }
    /// @brief Snapshot the metadata of one pool (free-list head, free count, bitmap).
    /// @param idx  Pool index.
    /// @param[out] out  Filled with the pool's current metadata.
    static void capture_pool_meta(size_t idx, PoolMeta& out);
    /// @brief Restore a pool's metadata from a previous snapshot and rebuild its free list.
    /// @param idx  Pool index.
    /// @param meta Previously captured metadata.
    static void restore_pool_meta(size_t idx, const PoolMeta& meta);
    /// @brief Copy all pool block data into a contiguous external buffer.
    /// @param[out] dst  Destination buffer (size must be >= pool_data_bytes()).
    static void capture_pool_data(uint8_t* dst);
    /// @brief Overwrite all pool block data from a contiguous external buffer.
    /// @param src  Source buffer previously filled by capture_pool_data().
    static void restore_pool_data(const uint8_t* src);
    /// @brief Calculate total bytes needed to hold all pool block data.
    /// @return Sum of (block_size * block_count) over all initialised pools.
    static size_t pool_data_bytes();

private:
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static Pool pools_[POOL_COUNT];
    static constinit bool ready_;

    /// @brief Finds the smallest pool class that satisfies a given size.
    ///        Linear scan over POOL_COUNT (9) entries — O(1) with small constant.
    /// @param size Requested allocation size.
    /// @return Pool index, or POOL_COUNT if too large.
    static size_t find_pool(size_t size);
};

} // namespace kernel
