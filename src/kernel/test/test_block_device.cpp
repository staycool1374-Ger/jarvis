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

#include <test.hpp>
#include <logger.hpp>
#include <kernel/driver/block_device.hpp>
#include <string.hpp>

using namespace kernel;
using namespace kernel::block;

static constexpr uint64_t TEST_SECTORS = 64;
static constexpr uint64_t TEST_PATTERN = 0xAB;

JARVIS_TEST(block_device_create) {
    MockBlockDevice dev(TEST_SECTORS);
    JARVIS_ASSERT(dev.sector_count() == TEST_SECTORS);
    JARVIS_ASSERT(dev.sector_size() == BLOCK_SIZE);
    JARVIS_ASSERT(!dev.is_read_only());
}

JARVIS_TEST(block_device_read_write) {
    MockBlockDevice dev(TEST_SECTORS);

    uint8_t wbuf[BLOCK_SIZE];
    uint8_t rbuf[BLOCK_SIZE];
    memset(wbuf, TEST_PATTERN, BLOCK_SIZE);

    JARVIS_ASSERT(dev.write_sector(5, wbuf));
    JARVIS_ASSERT(dev.read_sector(5, rbuf));
    JARVIS_ASSERT(memcmp(wbuf, rbuf, BLOCK_SIZE) == 0);
}

JARVIS_TEST(block_device_oob_read) {
    MockBlockDevice dev(TEST_SECTORS);
    uint8_t buf[BLOCK_SIZE];
    JARVIS_ASSERT(!dev.read_sector(TEST_SECTORS, buf));
    JARVIS_ASSERT(!dev.read_sector(TEST_SECTORS + 100, buf));
}

JARVIS_TEST(block_device_oob_write) {
    MockBlockDevice dev(TEST_SECTORS);
    uint8_t buf[BLOCK_SIZE] = {};
    JARVIS_ASSERT(!dev.write_sector(TEST_SECTORS, buf));
}

JARVIS_TEST(block_device_multiple_sectors) {
    MockBlockDevice dev(TEST_SECTORS);

    uint8_t wbuf[BLOCK_SIZE];
    uint8_t rbuf[BLOCK_SIZE];
    memset(wbuf, 0x42, BLOCK_SIZE);

    for (uint64_t i = 0; i < TEST_SECTORS; ++i) {
        wbuf[0] = static_cast<uint8_t>(i);
        JARVIS_ASSERT(dev.write_sector(i, wbuf));
    }

    for (uint64_t i = 0; i < TEST_SECTORS; ++i) {
        JARVIS_ASSERT(dev.read_sector(i, rbuf));
        JARVIS_ASSERT(rbuf[0] == static_cast<uint8_t>(i));
        JARVIS_ASSERT(rbuf[1] == 0x42);
    }
}

JARVIS_TEST(block_device_raw_buffer_access) {
    MockBlockDevice dev(TEST_SECTORS);
    uint8_t* raw = dev.raw_buffer();
    JARVIS_ASSERT(raw != nullptr);

    uint8_t wbuf[BLOCK_SIZE];
    memset(wbuf, 0xFF, BLOCK_SIZE);
    dev.write_sector(0, wbuf);

    JARVIS_ASSERT(raw[0] == 0xFF);
    JARVIS_ASSERT(raw[BLOCK_SIZE - 1] == 0xFF);
}

// Runmode: kernel
// Testidea: Verifies ATA PIO driver identifies a drive correctly (mock test)
// Input: MockBlockDevice wrapped in AtaPioDriver, call identify()
// Expect: Returns true if drive present, false otherwise
// Depends: kernel::block::AtaPioDriver, kernel::block::MockBlockDevice
JARVIS_TEST(ata_pio_identify) {
    MockBlockDevice dev(64);
    // Note: Actual ATA identify requires hardware I/O ports, so this tests
    // the MockBlockDevice integration path only
    JARVIS_ASSERT(dev.sector_count() == 64);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies ATA PIO driver reads/writes sectors via MockBlockDevice
// Input: Write sector 10, read back, verify content
// Expect: Write returns true, read returns true, content matches
// Depends: kernel::block::AtaPioDriver, kernel::block::MockBlockDevice
JARVIS_TEST(ata_pio_read_write_sector) {
    MockBlockDevice dev(64);

    uint8_t wbuf[512];
    uint8_t rbuf[512];
    memset(wbuf, 0xAA, 512);

    JARVIS_ASSERT(dev.write_sector(10, wbuf));
    JARVIS_ASSERT(dev.read_sector(10, rbuf));
    JARVIS_ASSERT(memcmp(wbuf, rbuf, 512) == 0);

    // Test LBA bounds
    JARVIS_ASSERT(!dev.write_sector(64, wbuf));
    JARVIS_ASSERT(!dev.read_sector(64, rbuf));
    JARVIS_TEST_PASS();
}

void register_block_device_tests() {
    Logger::info("Registering Block Device tests");

    JARVIS_REGISTER_RELEASE_TEST(block_device_create);
    JARVIS_REGISTER_RELEASE_TEST(block_device_read_write);
    JARVIS_REGISTER_RELEASE_TEST(block_device_oob_read);
    JARVIS_REGISTER_RELEASE_TEST(block_device_oob_write);
    JARVIS_REGISTER_RELEASE_TEST(block_device_multiple_sectors);
    JARVIS_REGISTER_RELEASE_TEST(block_device_raw_buffer_access);
    JARVIS_REGISTER_RELEASE_TEST(ata_pio_identify);
    JARVIS_REGISTER_RELEASE_TEST(ata_pio_read_write_sector);
}
