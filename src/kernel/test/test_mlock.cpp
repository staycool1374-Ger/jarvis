#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: SYS_MLOCK pins a page range in RAM.
// Pseudocode: Allocate a page via PMM. Call SYS_MLOCK on it. Verify the page
//   remains accessible. Call SYS_MUNLOCK. Verify no side effects.
// Input: One page at current task's program_break.
// Expect: SYS_MLOCK returns 0 (success) for valid range.
// Depends: SYS_MLOCK syscall (not yet implemented)
JARVIS_TEST(mlock_page_range, "PRE: none | POST: none | PENDING: SYS_MLOCK") {
    /* Pseudocode:
     *   uint64_t addr = SYS_BRK(0);
     *   int ret = syscall(SYS_MLOCK, addr, PAGE_SIZE);
     *   JARVIS_ASSERT_EQ(0, ret);
     *   // verify page is accessible
     *   volatile uint8_t* p = (volatile uint8_t*)addr;
     *   p[0] = 0xAB;
     *   JARVIS_ASSERT_EQ(0xAB, p[0]);
     *   syscall(SYS_MUNLOCK, addr, PAGE_SIZE);
     */
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: SYS_MLOCKALL pins all task pages.
// Pseudocode: Call SYS_MLOCKALL. Verify that faults on existing heap
//   pages do not cause swapping (if swap exists). Call SYS_MUNLOCKALL.
// Input: Current task's entire address space.
// Expect: SYS_MLOCKALL returns 0.
// Depends: SYS_MLOCKALL syscall (not yet implemented)
JARVIS_TEST(mlock_all_pages, "PRE: none | POST: none | PENDING: SYS_MLOCKALL") {
    /* Pseudocode:
     *   int ret = syscall(SYS_MLOCKALL, 0);
     *   JARVIS_ASSERT_EQ(0, ret);
     *   // verify no page fault on access
     *   syscall(SYS_MUNLOCKALL, 0);
     */
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Double-lock of an already-locked page is idempotent.
// Pseudocode: Lock a page twice, then unlock once. Page should still be pinned.
//   Second unlock should fully release.
// Input: Same page range locked twice.
// Expect: Second lock returns 0 (idempotent), second unlock succeeds.
// Depends: SYS_MLOCK, SYS_MUNLOCK (not yet implemented)
JARVIS_TEST(mlock_double_lock_idempotent, "PRE: none | POST: none | PENDING: SYS_MLOCK") {
    /* Pseudocode:
     *   uint64_t addr = SYS_BRK(0);
     *   JARVIS_ASSERT_EQ(0, syscall(SYS_MLOCK, addr, PAGE_SIZE));
     *   JARVIS_ASSERT_EQ(0, syscall(SYS_MLOCK, addr, PAGE_SIZE));
     *   JARVIS_ASSERT_EQ(0, syscall(SYS_MUNLOCK, addr, PAGE_SIZE));
     *   JARVIS_ASSERT_EQ(0, syscall(SYS_MUNLOCK, addr, PAGE_SIZE));
     */
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Unlocking a non-pinned page returns EINVAL.
// Pseudocode: Call SYS_MUNLOCK on an address that was never locked.
// Input: Address range not previously locked.
// Expect: SYS_MUNLOCK returns -EINVAL.
// Depends: SYS_MUNLOCK (not yet implemented)
JARVIS_TEST(mlock_unlock_nonpinned, "PRE: none | POST: none | PENDING: SYS_MUNLOCK") {
    /* Pseudocode:
     *   uint64_t addr = SYS_BRK(0) + PAGE_SIZE;
     *   int ret = syscall(SYS_MUNLOCK, addr, PAGE_SIZE);
     *   JARVIS_ASSERT_EQ(-EINVAL, ret);
     */
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Locking an invalid address range returns ENOMEM.
// Pseudocode: Call SYS_MLOCK with addr=0, size=0xFFFFFFFFFFFFFFFF.
// Input: Address range that wraps or exceeds addressable space.
// Expect: SYS_MLOCK returns -ENOMEM.
// Depends: SYS_MLOCK (not yet implemented)
JARVIS_TEST(mlock_invalid_range, "PRE: none | POST: none | PENDING: SYS_MLOCK") {
    /* Pseudocode:
     *   int ret = syscall(SYS_MLOCK, (uint64_t)-PAGE_SIZE, PAGE_SIZE);
     *   JARVIS_ASSERT_EQ(-ENOMEM, ret);
     */
    JARVIS_TEST_PASS();
}

void register_mlock_tests() {
    Logger::info("Registering mlock tests");
    JARVIS_REGISTER_TEST(mlock_page_range);
    JARVIS_REGISTER_TEST(mlock_all_pages);
    JARVIS_REGISTER_TEST(mlock_double_lock_idempotent);
    JARVIS_REGISTER_TEST(mlock_unlock_nonpinned);
    JARVIS_REGISTER_TEST(mlock_invalid_range);
}
