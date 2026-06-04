#include <kernel/memory/mempool.hpp>
#include <kernel/memory/pmm.hpp>
#include <assert.hpp>

namespace kernel {

MemPool::Pool MemPool::pools_[POOL_COUNT] = {};

void MemPool::init() {
    static const size_t sizes[POOL_COUNT] = {
        16, 32, 64, 128, 256, 512, 1024, 2048
    };
    static const size_t counts[POOL_COUNT] = {
        256, 128, 64, 32, 16, 8, 4, 2
    };

    for (size_t i = 0; i < POOL_COUNT; ++i) {
        auto& pool = pools_[i];
        pool.block_size = sizes[i];
        pool.block_count = counts[i];
        pool.free_count = counts[i];
        pool.first_free = 0;

        uint64_t phys = PMM::alloc_page();
        ASSERT(phys != 0);

        pool.data = reinterpret_cast<uint8_t*>(phys + 0xFFFF800000000000);

        pool.next_free = new size_t[pool.block_count];

        for (size_t j = 0; j < pool.block_count; ++j) {
            pool.next_free[j] = j + 1;
        }
        pool.next_free[pool.block_count - 1] = static_cast<size_t>(-1);

        pool.initialized = true;
    }
}

void* MemPool::alloc(size_t size) {
    size_t idx = find_pool(size);
    if (idx >= POOL_COUNT) return nullptr;

    auto& pool = pools_[idx];
    if (pool.free_count == 0) return nullptr;

    size_t block = pool.first_free;
    pool.first_free = pool.next_free[block];
    pool.next_free[block] = static_cast<size_t>(-1);
    --pool.free_count;

    return pool.data + block * pool.block_size;
}

void MemPool::free(void* ptr) {
    if (!ptr) return;

    for (size_t i = 0; i < POOL_COUNT; ++i) {
        auto& pool = pools_[i];
        if (!pool.initialized) continue;

        uint8_t* start = pool.data;
        uint8_t* end = pool.data + pool.block_size * pool.block_count;
        uint8_t* p = static_cast<uint8_t*>(ptr);

        if (p >= start && p < end) {
            size_t block = (p - start) / pool.block_size;
            pool.next_free[block] = pool.first_free;
            pool.first_free = block;
            ++pool.free_count;
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

} // namespace kernel
