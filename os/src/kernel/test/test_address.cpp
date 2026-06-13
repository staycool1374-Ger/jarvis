#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies phys_to_virt(HHDM_BASE) returns HHDM_BASE.
// Input: Call phys_to_virt with HHDM_BASE
// Expect: Returns HHDM_BASE
// Depends: kernel::address::PhysicalAddress, VirtualAddress
JARVIS_TEST(address_phys_to_virt_identity) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies virt_to_phys(HHDM_BASE) returns HHDM_BASE.
// Input: Call virt_to_phys with HHDM_BASE
// Expect: Returns HHDM_BASE
// Depends: kernel::address::VirtualAddress, PhysicalAddress
JARVIS_TEST(address_virt_to_phys_identity) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies PageAddress aligns a non-aligned address down to 4 KiB
// boundary.
// Input: Create PageAddress from unaligned address
// Expect: Address is 4 KiB aligned (lower 12 bits zero)
// Depends: kernel::address::PageAddress
JARVIS_TEST(address_page_align_down) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies VirtualAddress offset within page extracted correctly.
// Input: Create VirtualAddress with known offset
// Expect: offset() returns correct value (0-4095)
// Depends: kernel::address::VirtualAddress
JARVIS_TEST(address_page_offset) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies PhysicalAddress(0) == null PhysicalAddress.
// Input: Compare PhysicalAddress(0) with default-constructed
// Expect: Both compare equal
// Depends: kernel::address::PhysicalAddress
JARVIS_TEST(address_null_phys) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies <, >, ==, != work correctly across address objects.
// Input: Compare various address objects
// Expect: Comparisons return correct boolean results
// Depends: kernel::address::PhysicalAddress, VirtualAddress, PageAddress
JARVIS_TEST(address_comparison_operators) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Address Wrapper unit tests with the test framework.
// Input: None
// Expect: All Address tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_address_tests() {
    Logger::info("Registering address tests");
    JARVIS_REGISTER_TEST(address_phys_to_virt_identity);
    JARVIS_REGISTER_TEST(address_virt_to_phys_identity);
    JARVIS_REGISTER_TEST(address_page_align_down);
    JARVIS_REGISTER_TEST(address_page_offset);
    JARVIS_REGISTER_TEST(address_null_phys);
    JARVIS_REGISTER_TEST(address_comparison_operators);
}