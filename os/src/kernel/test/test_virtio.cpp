#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/virtio.hpp>
#include <kernel/driver/virtio_blk.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies that no Virtio block device is found on default QEMU
// (which only has IDE/SATA, no virtio-blk). Ensures virtio_find_device
// returns false gracefully.
// Input: arch::virtio_find_device(VIRTIO_DEVICE_BLOCK, transport)
// Expect: Returns false (no virtio device)
// Depends: arch::pci_scan_all, arch::virtio_find_device
JARVIS_TEST(virtio_no_device_found) {
    arch::VirtioTransport transport;
    bool found = arch::virtio_find_device(arch::VIRTIO_DEVICE_BLOCK, transport);
    JARVIS_ASSERT(!found);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies virtio_is_virtio_device returns false for non-virtio
// devices like the host bridge.
// Input: arch::pci_read_device_info for 00:00.0, then check
// Expect: virtio_is_virtio_device returns false
// Depends: arch::pci_read_device_info, arch::virtio_is_virtio_device
JARVIS_TEST(virtio_not_virtio_device) {
    arch::PciBdf bdf = {0, 0, 0};
    arch::PciDeviceInfo info = arch::pci_read_device_info(bdf);
    JARVIS_ASSERT(!arch::virtio_is_virtio_device(info));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies virtio probe returns nullptr when no virtio-blk device
// is present.
// Input: kernel::block::VirtioBlkDriver::probe()
// Expect: Returns nullptr
// Depends: arch::pci_scan_all, arch::virtio_find_device
JARVIS_TEST(virtio_blk_probe_no_device) {
    auto* drv = kernel::block::VirtioBlkDriver::probe();
    JARVIS_ASSERT(drv == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies virtio_map_mmio returns false for invalid BAR.
// Input: BAR=6 (out of range), invalid BDF
// Expect: Returns false
// Depends: arch::virtio_map_mmio
JARVIS_TEST(virtio_map_mmio_invalid) {
    arch::VirtioMmio mmio;
    arch::PciBdf bdf = {0, 0, 0};
    bool ok = arch::virtio_map_mmio(6, 0, 4096, mmio, bdf);
    JARVIS_ASSERT(!ok);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies virtio_init_transport returns true even on a
// device that isn't a real Virtio device (it only writes status).
// Input: Default-constructed VirtioTransport
// Expect: Returns true (harmless without real device)
// Depends: arch::virtio_init_transport
JARVIS_TEST(virtio_init_transport_noop) {
    arch::VirtioTransport t = {};
    bool ok = arch::virtio_init_transport(t);
    JARVIS_ASSERT(ok);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Virtio tests with the test framework.
// Input: None
// Expect: All Virtio tests are registered (no direct assertion, only logging)
// Depends: Test framework, Logger, arch::virtio
void register_virtio_tests() {
    Logger::info("Registering Virtio tests");
    JARVIS_REGISTER_TEST(virtio_no_device_found);
    JARVIS_REGISTER_TEST(virtio_not_virtio_device);
    JARVIS_REGISTER_TEST(virtio_blk_probe_no_device);
    JARVIS_REGISTER_TEST(virtio_map_mmio_invalid);
    JARVIS_REGISTER_TEST(virtio_init_transport_noop);
}
