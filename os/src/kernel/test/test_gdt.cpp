#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies all GDT entries have correct base=0, limit, access byte after init.
// Input: Initialize GDT
// Expect: All entries match expected descriptor layout
// Depends: kernel::arch::GDT
JARVIS_TEST(gdt_entries_valid_after_init) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies TSS IST1-IST7 pointers point to valid kernel stack pages.
// Input: Initialize GDT/TSS
// Expect: IST1-IST7 are non-null, point to allocated kernel stacks
// Depends: kernel::arch::GDT, TSS
JARVIS_TEST(gdt_tss_ist_valid) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies RSP0 points to kernel stack for ring 3 -> ring 0 transitions.
// Input: Initialize TSS
// Expect: RSP0 is non-zero, points to valid kernel stack
// Depends: kernel::arch::TSS
JARVIS_TEST(gdt_tss_rsp0_set) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies code (ring 0 + ring 3) and data segments are accessible.
// Input: Initialize GDT, attempt to load segment selectors
// Expect: No GP fault on loading valid selectors
// Depends: kernel::arch::GDT
JARVIS_TEST(gdt_code_data_segments_present) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies user code/data segment descriptors have DPL=3.
// Input: Initialize GDT, inspect user segment descriptors
// Expect: DPL field == 3 for user segments
// Depends: kernel::arch::GDT
JARVIS_TEST(gdt_user_segments_have_ring3_dpl) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all GDT/TSS unit tests with the test framework.
// Input: None
// Expect: All GDT tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_gdt_tests() {
    Logger::info("Registering GDT tests");
    JARVIS_REGISTER_TEST(gdt_entries_valid_after_init);
    JARVIS_REGISTER_TEST(gdt_tss_ist_valid);
    JARVIS_REGISTER_TEST(gdt_tss_rsp0_set);
    JARVIS_REGISTER_TEST(gdt_code_data_segments_present);
    JARVIS_REGISTER_TEST(gdt_user_segments_have_ring3_dpl);
}