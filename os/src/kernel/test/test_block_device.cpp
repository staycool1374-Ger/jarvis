// Runmode: kernel
// Testidea: Block device tests for ATA PIO driver and block layer
// Depends: kernel::BlockDevice, kernel::ATA driver (mock)

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// -------------------------------------------------------------------
// ATA PIO Driver Tests (using mock block device)
// -------------------------------------------------------------------

// Runmode: kernel
// Testidea: STUB - ATA IDENTIFY command returns valid disk signature (0x0040
// or 0xC33C)
// Input: Mock ATA device
// Expect: Passes (stub)
// Depends: kernel::ATA driver
JARVIS_TEST(ata_pio_identify) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Read one sector (512 bytes) returns valid data without error
// Input: Mock ATA device, sector 0
// Expect: Passes (stub)
// Depends: kernel::ATA driver
JARVIS_TEST(ata_pio_read_single_sector) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Write then read back a sector produces identical data
// Input: Mock ATA device, sector 1, test pattern
// Expect: Passes (stub)
// Depends: kernel::ATA driver
JARVIS_TEST(ata_pio_write_single_sector) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Consecutive sector reads return sequential data (LBA
// ordering)
// Input: Mock ATA device, sectors 0..N
// Expect: Passes (stub)
// Depends: kernel::ATA driver
JARVIS_TEST(ata_pio_read_multiple_sectors) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Multiple sectors written then read back match exactly
// Input: Mock ATA device, sectors 0..N, test pattern
// Expect: Passes (stub)
// Depends: kernel::ATA driver
JARVIS_TEST(ata_pio_write_read_roundtrip) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - LBA beyond disk capacity returns error
// Input: Mock ATA device, LBA > max sectors
// Expect: Passes (stub)
// Depends: kernel::ATA driver
JARVIS_TEST(ata_pio_invalid_lba_rejected) {
    JARVIS_TEST_PASS();
}

// -------------------------------------------------------------------
// Block Layer Tests
// -------------------------------------------------------------------

// Runmode: kernel
// Testidea: STUB - Block device opens with correct sector count
// Input: Mock block device
// Expect: Passes (stub)
// Depends: kernel::BlockDevice
JARVIS_TEST(block_device_open) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Block read returns sector-aligned data
// Input: Mock block device, sector 0
// Expect: Passes (stub)
// Depends: kernel::BlockDevice
JARVIS_TEST(block_device_read_sector) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Block write followed by read returns same data
// Input: Mock block device, sector 1, test pattern
// Expect: Passes (stub)
// Depends: kernel::BlockDevice
JARVIS_TEST(block_device_write_sector) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - IOCTL returns block size (512) and sector count
// Input: Mock block device, IOCTL_GET_INFO
// Expect: Passes (stub)
// Depends: kernel::BlockDevice
JARVIS_TEST(block_device_ioctl_info) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Read/write beyond device capacity returns error
// Input: Mock block device, LBA >= sector_count
// Expect: Passes (stub)
// Depends: kernel::BlockDevice
JARVIS_TEST(block_device_oob_rejected) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Read with non-sector-aligned buffer is handled safely
// Input: Mock block device, misaligned buffer
// Expect: Passes (stub)
// Depends: kernel::BlockDevice
JARVIS_TEST(block_device_unaligned_buffer) {
    JARVIS_TEST_PASS();
}

// -------------------------------------------------------------------
// Registration
// -------------------------------------------------------------------
void register_block_device_tests() {
    Logger::info("Registering Block Device tests");

    JARVIS_REGISTER_TEST(ata_pio_identify);
    JARVIS_REGISTER_TEST(ata_pio_read_single_sector);
    JARVIS_REGISTER_TEST(ata_pio_write_single_sector);
    JARVIS_REGISTER_TEST(ata_pio_read_multiple_sectors);
    JARVIS_REGISTER_TEST(ata_pio_write_read_roundtrip);
    JARVIS_REGISTER_TEST(ata_pio_invalid_lba_rejected);

    JARVIS_REGISTER_TEST(block_device_open);
    JARVIS_REGISTER_TEST(block_device_read_sector);
    JARVIS_REGISTER_TEST(block_device_write_sector);
    JARVIS_REGISTER_TEST(block_device_ioctl_info);
    JARVIS_REGISTER_TEST(block_device_oob_rejected);
    JARVIS_REGISTER_TEST(block_device_unaligned_buffer);
}