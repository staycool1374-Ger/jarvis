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

#include <types.hpp>
#include <string.hpp>
#include <assert.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>

// Bump allocator — used only before MemPool is initialized (early boot).
static uint8_t heap_region[1_MiB] __attribute__((aligned(16)));
static uint64_t heap_offset = 0;
static bool heap_initialized = false;

extern "C" void init_heap() {
    heap_offset = 0;
    heap_initialized = true;
}

static bool mempool_ready() {
    return kernel::MemPool::is_ready();
}

// Header stored before PMM-backed allocations so delete knows how many
// pages to free. Lower bit flags: 0 = MemPool-owned (unused here).
struct PmmAllocHdr {
    uint64_t page_count;
};

static void* pmm_alloc(size_t size) {
    uint64_t pages = (size + sizeof(PmmAllocHdr) + arch::PAGE_SIZE - 1)
                     / arch::PAGE_SIZE;
    uint64_t phys = kernel::PMM::alloc_contiguous(pages);
    if (!phys) return nullptr;
    uint8_t* raw = reinterpret_cast<uint8_t*>(phys + arch::HHDM_OFFSET);
    auto* hdr = reinterpret_cast<PmmAllocHdr*>(raw);
    hdr->page_count = pages;
    void* ptr = raw + sizeof(PmmAllocHdr);
    memset(ptr, 0, pages * arch::PAGE_SIZE - sizeof(PmmAllocHdr));
    return ptr;
}

static void pmm_free(void* ptr) {
    auto* raw = static_cast<uint8_t*>(ptr);
    auto* hdr = reinterpret_cast<PmmAllocHdr*>(raw - sizeof(PmmAllocHdr));
    uint64_t pages = hdr->page_count;
    uint64_t phys = reinterpret_cast<uint64_t>(raw) - arch::HHDM_OFFSET
                    - sizeof(PmmAllocHdr);
    ENSURE(phys != 0 && (phys & (arch::PAGE_SIZE - 1)) == 0);
    for (uint64_t i = 0; i < pages; ++i) {
        kernel::PMM::free_page(phys + i * arch::PAGE_SIZE);
    }
}

void* operator new(unsigned long size) {
    if (size == 0) size = 1;
    size = (size + 15) & ~15ULL;

    if (mempool_ready()) {
        void* p = kernel::MemPool::alloc(size);
        if (p) return p;
        // Too large for MemPool — allocate from PMM directly
        p = pmm_alloc(size);
        if (p) return p;
    }

    // Fallback: bump allocator (early boot or OOM)
    if (!heap_initialized) init_heap();
    if (heap_offset + size > sizeof(heap_region)) {
        void* p = pmm_alloc(size);
        if (p) return p;
        constexpr size_t MAX_RETRIES = 1000000;
        size_t retries = 0;
        while (!p && retries < MAX_RETRIES) {
            arch::hlt();
            p = pmm_alloc(size);
            ++retries;
        }
        if (!p) { while (true) { arch::hlt(); } }
    }
    void* ptr = &heap_region[heap_offset];
    heap_offset += size;
    memset(ptr, 0, size);
    return ptr;
}

void* operator new[](unsigned long size) {
    return operator new(size);
}

void* operator new(unsigned long size, void* ptr) {
    (void)size;
    return ptr;
}

void* operator new[](unsigned long size, void* ptr) {
    (void)size;
    return ptr;
}

void operator delete(void* p) {
    if (!p) return;
    if (kernel::MemPool::contains(p)) {
        kernel::MemPool::free(p);
    } else {
        pmm_free(p);
    }
}

void operator delete[](void* p) { operator delete(p); }

void operator delete(void* p, unsigned long) { operator delete(p); }

void operator delete[](void* p, unsigned long) { operator delete(p); }
