#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/pci.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies PCI bus scan finds at least one device on QEMU's emulated
// PCI bus. QEMU's PC machine always has a host bridge (00:00.0) and typically
// several other devices.
// Input: Call arch::pci_scan_all()
// Expect: Returns > 0 devices found
// Depends: arch::PCI CF8/CFC config space access
JARVIS_TEST(pci_scan_finds_devices) {
    size_t count = arch::pci_scan_all();
    JARVIS_ASSERT(count > 0);
    Logger::info("PCI: %d devices found", count);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies vendor/device ID can be read for a known BDF. The host
// bridge at 00:00.0 should always return valid IDs on QEMU.
// Input: Read vendor/device for 00:00.0
// Expect: vendor_id != 0xFFFF and != 0, device_id != 0
// Depends: arch::pci_read_vendor, arch::pci_read_device
JARVIS_TEST(pci_read_host_bridge_ids) {
    arch::PciBdf bdf = {0, 0, 0};
    uint16_t vendor = arch::pci_read_vendor(bdf);
    uint16_t device = arch::pci_read_device(bdf);
    JARVIS_ASSERT(vendor != 0xFFFF);
    JARVIS_ASSERT(vendor != 0);
    JARVIS_ASSERT(device != 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies pci_read_device_info populates all fields for the host
// bridge at 00:00.0.
// Input: arch::pci_read_device_info({0,0,0})
// Expect: vendor_id != 0xFFFF, class_code is 0x06 (bridge), header_type = 0x00
// Depends: arch::pci_read_device_info
JARVIS_TEST(pci_read_host_bridge_info) {
    arch::PciBdf bdf = {0, 0, 0};
    arch::PciDeviceInfo info = arch::pci_read_device_info(bdf);
    JARVIS_ASSERT(info.vendor_id != 0xFFFF);
    JARVIS_ASSERT(info.class_code == 0x06);
    JARVIS_ASSERT(info.subclass == 0x00); // host bridge
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies the ISA bridge (typically 00:01.0 on QEMU's PIIX3) is
// found and identified correctly.
// Input: Call arch::pci_scan_all(), then search devices
// Expect: At least one device is a PCI-to-ISA bridge (class=0x06, subclass=0x01)
// Depends: arch::pci_scan_all, arch::pci_find_device
JARVIS_TEST(pci_finds_isa_bridge) {
    arch::pci_scan_all();
    const arch::PciDeviceInfo* isa = arch::pci_find_device(0x06, 0x01);
    JARVIS_ASSERT(isa != nullptr);
    JARVIS_ASSERT(isa->bdf.bus == 0);
    JARVIS_ASSERT(isa->bdf.device == 1);
    JARVIS_ASSERT(isa->bdf.function == 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a nonexistent BDF returns vendor 0xFFFF and
// pci_device_exists returns false.
// Input: Read vendor at BDF 255:31:7 (non-existent)
// Expect: vendor == 0xFFFF, pci_device_exists returns false
// Depends: arch::pci_read_vendor, arch::pci_device_exists
JARVIS_TEST(pci_nonexistent_bdf) {
    arch::PciBdf bdf = {255, 31, 7};
    uint16_t vendor = arch::pci_read_vendor(bdf);
    JARVIS_ASSERT_EQ(0xFFFF, vendor);
    JARVIS_ASSERT(!arch::pci_device_exists(bdf));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies the PCI command register can be read and contains at
// least the IO space bit (devices are typically enabled by firmware).
// Input: Read command register at 00:00.0
// Expect: command register != 0
// Depends: arch::pci_config_readw
JARVIS_TEST(pci_read_command_register) {
    arch::PciBdf bdf = {0, 0, 0};
    uint32_t addr = arch::pci_make_addr(bdf, arch::PCI_COMMAND);
    uint16_t cmd = arch::pci_config_readw(addr);
    // Host bridge should have at least I/O or memory space enabled
    JARVIS_ASSERT(cmd != 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies pci_make_addr constructs the correct config address.
// Input: BDF 1:2.3, register 0x10
// Expect: addr = 0x80000000 | (1 << 16) | (2 << 11) | (3 << 8) | 0x10
// Depends: arch::pci_make_addr
JARVIS_TEST(pci_make_addr_format) {
    arch::PciBdf bdf = {1, 2, 3};
    uint32_t addr = arch::pci_make_addr(bdf, 0x10);
    JARVIS_ASSERT_HEX_EQ(0x80000000U | (1U << 16) | (2U << 11) | (3U << 8) | 0x10U, addr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies pci_find_device returns nullptr for nonexistent class/subclass.
// Input: arch::pci_find_device(0xFF, 0xFF)
// Expect: Returns nullptr
// Depends: arch::pci_scan_all, arch::pci_find_device
JARVIS_TEST(pci_find_nonexistent_device) {
    arch::pci_scan_all();
    const arch::PciDeviceInfo* dev = arch::pci_find_device(0xFF, 0xFF);
    JARVIS_ASSERT(dev == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all PCI tests with the test framework.
// Input: None
// Expect: All PCI tests are registered (no direct assertion, only logging)
// Depends: Test framework, Logger, arch::PCI
void register_pci_tests() {
    Logger::info("Registering PCI tests");
    JARVIS_REGISTER_TEST(pci_scan_finds_devices);
    JARVIS_REGISTER_TEST(pci_read_host_bridge_ids);
    JARVIS_REGISTER_TEST(pci_read_host_bridge_info);
    JARVIS_REGISTER_TEST(pci_finds_isa_bridge);
    JARVIS_REGISTER_TEST(pci_nonexistent_bdf);
    JARVIS_REGISTER_TEST(pci_read_command_register);
    JARVIS_REGISTER_TEST(pci_make_addr_format);
    JARVIS_REGISTER_TEST(pci_find_nonexistent_device);
}
