#include <types.hpp>
#include <string.hpp>
#include <kernel/memory/pmm.hpp>
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
    if (!p) while (1) { arch::hlt(); }
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

void operator delete(void* p) { (void)p; }
void operator delete[](void* p) { (void)p; }
void operator delete(void* p, unsigned long) { (void)p; }
void operator delete[](void* p, unsigned long) { (void)p; }
