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

void register_block_device_tests() {
    Logger::info("Registering Block Device tests");

    JARVIS_REGISTER_RELEASE_TEST(block_device_create);
    JARVIS_REGISTER_RELEASE_TEST(block_device_read_write);
    JARVIS_REGISTER_RELEASE_TEST(block_device_oob_read);
    JARVIS_REGISTER_RELEASE_TEST(block_device_oob_write);
    JARVIS_REGISTER_RELEASE_TEST(block_device_multiple_sectors);
    JARVIS_REGISTER_RELEASE_TEST(block_device_raw_buffer_access);
}
