#include <kernel/memory/mempool.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/test/resource_tracker.hpp>
#include <assert.hpp>
#include <constants.hpp>

namespace kernel {

MemPool::Pool MemPool::pools_[POOL_COUNT] = {};

void MemPool::init() {
    static const size_t sizes[POOL_COUNT] = {
        16, 32, 64, 128, 256, 512, 1024, 2048, 4480
    };
    static const size_t counts[POOL_COUNT] = {
        256, 128, 64, 32, 16, 8, 16, 64, 16
    };

    for (size_t i = 0; i < POOL_COUNT; ++i) {
        auto& pool = pools_[i];
        pool.block_size = sizes[i];
        pool.block_count = counts[i];
        pool.free_count = counts[i];
        pool.first_free = 0;

        size_t pool_bytes = pool.block_count * pool.block_size;
        size_t pages = (pool_bytes + arch::PAGE_SIZE - 1) / arch::PAGE_SIZE;
        uint64_t phys = PMM::alloc_contiguous(pages);
        ENSURE(phys != 0);

        pool.data = reinterpret_cast<uint8_t*>(phys + arch::HHDM_OFFSET);

        pool.first_free = 0;
        for (size_t j = 0; j < pool.block_count; ++j) {
            size_t* next = reinterpret_cast<size_t*>(
                pool.data + j * pool.block_size);
            *next = j + 1;
            pool.set_block_freed(j);
        }
        {
            size_t* next = reinterpret_cast<size_t*>(pool.data + (
                pool.block_count - 1) * pool.block_size);
            *next = static_cast<size_t>(-1);
        }

        pool.initialized = true;
    }
}

void* MemPool::alloc(size_t size) {
    size_t idx = find_pool(size);
    if (idx >= POOL_COUNT) return nullptr;

    auto& pool = pools_[idx];
    if (pool.free_count == 0) return nullptr;

    size_t block = pool.first_free;
    ENSURE(block < pool.block_count);
    ENSURE(pool.is_block_freed(block)
           && "free-list corruption: alloc of already-allocated block");
    size_t* next = reinterpret_cast<size_t*>(pool.data + block * pool.block_size
        );
    pool.first_free = *next;
    --pool.free_count;
    pool.clear_block_freed(block);

    kernel::test::ResourceTracker::instance().track_mempool_alloc(idx);
    return pool.data + block * pool.block_size;
}

void MemPool::free(void* block) {
    if (!block) return;

    for (size_t i = 0; i < POOL_COUNT; ++i) {
        auto& pool = pools_[i];
        if (!pool.initialized) continue;

        uint8_t* start = pool.data;
        uint8_t* end = pool.data + pool.block_size * pool.block_count;
        uint8_t* p = static_cast<uint8_t*>(block);

        if (p >= start && p < end) {
            size_t offset = static_cast<size_t>(p - start);
            size_t block_idx = offset / pool.block_size;
            ENSURE(offset % pool.block_size == 0);
            ENSURE(block_idx < pool.block_count);
            ENSURE(!pool.is_block_freed(block_idx)
                   && "double-free detected");
#ifdef CONFIG_DEBUG
            __builtin_memset(p, 0xDD, pool.block_size);
#endif
            pool.set_block_freed(block_idx);
            size_t* next = reinterpret_cast<size_t*>(p);
            *next = pool.first_free;
            pool.first_free = block_idx;
            ++pool.free_count;
            kernel::test::ResourceTracker::instance().track_mempool_free(i);
            return;
        }
    }
}

size_t MemPool::find_pool(size_t size) {
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        if (pools_[i].block_size >= size && pools_[i].initialized) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

// ---------------------------------------------------------------------------
// Test-isolation helpers
// ---------------------------------------------------------------------------

void MemPool::capture_pool_meta(size_t idx, PoolMeta& out) {
    auto& p = pools_[idx];
    out.first_free  = p.first_free;
    out.free_count  = p.free_count;
    p.copy_freed_bitmap(out.freed_bitmap);
}

void MemPool::restore_pool_meta(size_t idx, const PoolMeta& meta) {
    auto& p = pools_[idx];
    p.first_free  = meta.first_free;
    p.free_count  = meta.free_count;
    p.write_freed_bitmap(meta.freed_bitmap);

    // Rebuild the free-list *next* pointers from the bitmap so the
    // pool is internally consistent even if test code wrote into
    // supposedly-allocated blocks that have now been restored to "free".
    // Build the list by prepending each freed block to the head so that
    // every freed block is reachable from first_free.
    p.first_free  = static_cast<size_t>(-1);
    for (size_t j = 0; j < p.block_count; ++j) {
        if (!p.is_block_freed(j)) continue;          // still allocated — skip
        auto* next = reinterpret_cast<uint64_t*>(
            p.data + j * p.block_size);
        *next = p.first_free;
        p.first_free = j;
    }
    // If the pool was completely full before snapshot the rebuild
    // leaves first_free == -1 (no free blocks) — correct.
}

void MemPool::capture_pool_data(uint8_t* dst) {
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        auto& p = pools_[i];
        if (!p.initialized || !p.data) continue;
        size_t bytes = p.block_count * p.block_size;
        __builtin_memcpy(dst, p.data, bytes);
        dst += bytes;
    }
}

void MemPool::restore_pool_data(const uint8_t* src) {
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        auto& p = pools_[i];
        if (!p.initialized || !p.data) continue;
        size_t bytes = p.block_count * p.block_size;
        __builtin_memcpy(p.data, src, bytes);
        src += bytes;
    }
}

size_t MemPool::pool_data_bytes() {
    size_t total = 0;
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        if (!pools_[i].initialized) continue;
        total += pools_[i].block_count * pools_[i].block_size;
    }
    return total;
}

} // namespace kernel
