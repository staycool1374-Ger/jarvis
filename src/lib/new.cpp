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
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>

static uint8_t heap_region[1_MiB] __attribute__((aligned(16)));
static uint64_t heap_offset = 0;
static bool heap_initialized = false;

extern "C" void init_heap() {
    heap_offset = 0;
    heap_initialized = true;
}

static void* heap_alloc(size_t size) {
    if (heap_offset + size > sizeof(heap_region)) {
        uint64_t pages = (size + 4095) / arch::PAGE_SIZE;
        uint64_t phys = kernel::PMM::alloc_contiguous(pages);
        if (!phys) return nullptr;
        memset(reinterpret_cast<void*>(phys + arch::HHDM_OFFSET), 0, pages * arch::PAGE_SIZE);
        return reinterpret_cast<void*>(phys + arch::HHDM_OFFSET);
    }
    void* ptr = &heap_region[heap_offset];
    heap_offset += size;
    memset(ptr, 0, size);
    return ptr;
}

void* operator new(unsigned long size) {
    if (!heap_initialized) init_heap();
    if (size > ~0ULL - 15) size = 1;
    size = (size + 15) & ~15ULL;
    void* p = heap_alloc(size);
    constexpr size_t MAX_RETRIES = 1000000;
    size_t retries = 0;
    while (!p && retries < MAX_RETRIES) {
        arch::hlt();
        p = heap_alloc(size);
        ++retries;
    }
    if (!p) { while (true) { arch::hlt(); } }
    return p;
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

void operator delete(void* p) { kernel::MemPool::free(p); }
void operator delete[](void* p) { kernel::MemPool::free(p); }
void operator delete(void* p, unsigned long) { kernel::MemPool::free(p); }
void operator delete[](void* p, unsigned long) { kernel::MemPool::free(p); }
