#include <test.hpp>
#include <logger.hpp>
#include <kernel/memory/pmm.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies freeing an already-freed page is idempotent (no crash).
// Input: Allocate page, free it, free it again
// Expect: No crash, second free is no-op
// Depends: kernel::memory::PMM
JARVIS_TEST(pmm_double_free_rejected) {
    uint64_t page = PMM::alloc_page();
    JARVIS_ASSERT(page != 0);
    PMM::free_page(page);
    PMM::free_page(page);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies freeing a non-allocated/invalid page address is safe.
// Input: Call free_page with invalid address (beyond total memory)
// Expect: No crash, silently returns
// Depends: kernel::memory::PMM
JARVIS_TEST(pmm_free_invalid_address) {
    PMM::free_page(~0ULL);
    PMM::free_page(0xFFFFFFFFFFFFF000ULL);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies alloc_contiguous(0) returns 0.
// Input: Call alloc_contiguous(0)
// Expect: Returns 0
// Depends: kernel::memory::PMM
JARVIS_TEST(pmm_alloc_contiguous_zero) {
    uint64_t page = PMM::alloc_contiguous(0);
    JARVIS_ASSERT_EQ(0ULL, page);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies allocate beyond available memory returns 0.
// Input: Allocate until exhausted, then one more
// Expect: Returns 0
// Depends: kernel::memory::PMM
JARVIS_TEST(pmm_alloc_contiguous_exhaustion) {
    uint64_t pages[64];
    size_t count = 0;
    while (count < 64) {
        uint64_t p = PMM::alloc_contiguous(1);
        if (p == 0) break;
        pages[count++] = p;
    }
    uint64_t extra = PMM::alloc_contiguous(1);
    JARVIS_ASSERT_EQ(0ULL, extra);
    for (size_t i = 0; i < count; ++i) {
        PMM::free_page(pages[i]);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies user page allocation fails when pool exhausted.
// Input: Exhaust memory via alloc_user_page, then one more
// Expect: Returns 0 (same pool as kernel)
// Depends: kernel::memory::PMM
JARVIS_TEST(pmm_alloc_user_page_oob) {
    uint64_t pages[64];
    size_t count = 0;
    while (count < 64) {
        uint64_t p = PMM::alloc_user_page();
        if (p == 0) break;
        pages[count++] = p;
    }
    uint64_t extra = PMM::alloc_user_page();
    JARVIS_ASSERT_EQ(0ULL, extra);
    for (size_t i = 0; i < count; ++i) {
        PMM::free_page(pages[i]);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies page-table pages come from <128 MiB pool.
// Input: Allocate page table page, check address
// Expect: Physical address < 128 MiB (pool at 16 MiB from low mem)
// Depends: kernel::memory::PMM
JARVIS_TEST(pmm_page_table_alloc_from_low_mem) {
    uint64_t page = PMM::alloc_page_table();
    JARVIS_ASSERT(page != 0);
    JARVIS_ASSERT(page < 128 * 1024 * 1024);
    PMM::free_page(page);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all PMM unit tests with the test framework.
// Input: None
// Expect: All PMM tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_pmm_tests() {
    Logger::info("Registering PMM tests");
    JARVIS_REGISTER_TEST(pmm_double_free_rejected);
    JARVIS_REGISTER_TEST(pmm_free_invalid_address);
    JARVIS_REGISTER_TEST(pmm_alloc_contiguous_zero);
    JARVIS_REGISTER_TEST(pmm_alloc_contiguous_exhaustion);
    JARVIS_REGISTER_TEST(pmm_alloc_user_page_oob);
    JARVIS_REGISTER_TEST(pmm_page_table_alloc_from_low_mem);
}