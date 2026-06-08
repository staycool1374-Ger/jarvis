#include <test.hpp>
#include <logger.hpp>
#include <constants.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>

using namespace kernel;

JARVIS_TEST(pmm_alloc_free) {
    uint64_t before = PMM::free_memory();
    uint64_t p1 = PMM::alloc_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(PMM::free_memory() == before - 4096);
    PMM::free_page(p1);
    JARVIS_ASSERT(PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_alloc_contiguous) {
    uint64_t before = PMM::free_memory();
    uint64_t pages = PMM::alloc_contiguous(4);
    JARVIS_ASSERT(pages != 0);
    JARVIS_ASSERT(PMM::free_memory() <= before - 4 * 4096);
    for (size_t i = 0; i < 4; ++i)
        PMM::free_page(pages + i * 4096);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_user_alloc) {
    uint64_t before = PMM::free_memory();
    uint64_t p1 = PMM::alloc_user_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(PMM::is_user_page(p1));
    PMM::free_page(p1);
    JARVIS_ASSERT(PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_total_memory) {
    uint64_t total = PMM::total_memory();
    JARVIS_ASSERT(total > 0);
    JARVIS_ASSERT(total >= PMM::free_memory());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vmm_free_user_pages_skips_kernel_owned_entries) {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    uint64_t user_page = PMM::alloc_user_page();
    JARVIS_ASSERT(user_page != 0);
    VMM::map_page_in_pml4(0x8000000000ULL, user_page, true, pml4);

    VMM::free_user_pages(pml4);

    PMM::free_page(pml4);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vmm_free_user_pages_fork_stack_scenario) {
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);

    uint64_t stack_page = PMM::alloc_user_page();
    JARVIS_ASSERT(stack_page != 0);
    VMM::map_page_in_pml4(mem::STACK_VADDR, stack_page, true, pml4);

    VMM::free_user_pages(pml4);

    PMM::free_page(pml4);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_alloc_free) {
    void* p1 = MemPool::alloc(16);
    JARVIS_ASSERT(p1 != nullptr);
    void* p2 = MemPool::alloc(32);
    JARVIS_ASSERT(p2 != nullptr);
    JARVIS_ASSERT(p2 != p1);
    MemPool::free(p1);
    MemPool::free(p2);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_large_alloc) {
    void* p = MemPool::alloc(1024);
    JARVIS_ASSERT(p != nullptr);
    MemPool::free(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_fragmentation) {
    void* ptrs[10];
    for (int i = 0; i < 10; ++i) {
        ptrs[i] = MemPool::alloc(64);
        JARVIS_ASSERT(ptrs[i] != nullptr);
    }
    for (int i = 0; i < 10; ++i)
        MemPool::free(ptrs[i]);
    void* p = MemPool::alloc(64);
    JARVIS_ASSERT(p != nullptr);
    MemPool::free(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_reuse) {
    void* p1 = MemPool::alloc(16);
    JARVIS_ASSERT(p1 != nullptr);
    MemPool::free(p1);
    void* p2 = MemPool::alloc(16);
    JARVIS_ASSERT(p2 != nullptr);
    JARVIS_ASSERT(p1 == p2);
    MemPool::free(p2);
    JARVIS_TEST_PASS();
}

void register_memory_tests() {
    Logger::info("Registering memory tests");

    JARVIS_REGISTER_TEST(pmm_alloc_free);
    JARVIS_REGISTER_TEST(pmm_alloc_contiguous);
    JARVIS_REGISTER_TEST(pmm_user_alloc);
    JARVIS_REGISTER_TEST(pmm_total_memory);
    JARVIS_REGISTER_TEST(vmm_free_user_pages_skips_kernel_owned_entries);
    JARVIS_REGISTER_TEST(vmm_free_user_pages_fork_stack_scenario);

    JARVIS_REGISTER_TEST(mempool_alloc_free);
    JARVIS_REGISTER_TEST(mempool_large_alloc);
    JARVIS_REGISTER_TEST(mempool_fragmentation);
    JARVIS_REGISTER_TEST(mempool_reuse);
}
