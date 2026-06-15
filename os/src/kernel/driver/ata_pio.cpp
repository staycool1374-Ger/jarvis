#include <kernel/driver/ata_pio.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>
#include <string.hpp>

namespace kernel {
namespace block {

static constexpr uint64_t ATA_POLL_TIMEOUT = 100000;

AtaPioDriver::AtaPioDriver() = default;

using namespace arch;

bool AtaPioDriver::identify() {
    outb(ATA_DRIVE, ATA_DRIVE_MASTER);

    outb(ATA_SECTOR_CNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);

    outb(ATA_CMD, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_CMD);
    if (status == 0) return false;

    if (!poll_status(true)) return false;
    if (status & ATA_SR_ERR) return false;

    if (!wait_for_drq()) return false;

    uint16_t buf[256];
    for (int i = 0; i < 256; ++i) {
        buf[i] = inw(ATA_DATA);
    }

    sector_count_ = (static_cast<uint64_t>(buf[61]) << 16) | buf[60];
    drive_present_ = true;
    return true;
}

bool AtaPioDriver::init() {
    return identify();
}

bool AtaPioDriver::poll_status(bool wait_for_bsy) {
    for (uint64_t i = 0; i < ATA_POLL_TIMEOUT; ++i) {
        uint8_t status = inb(ATA_CMD);
        if (wait_for_bsy) {
            if (!(status & ATA_SR_BSY)) return true;
        } else {
            if (status & ATA_SR_BSY) continue;
            if (status & ATA_SR_ERR) return false;
            if (status & ATA_SR_DRQ) return true;
        }
        io_wait();
    }
    return false;
}

bool AtaPioDriver::wait_for_drq() {
    return poll_status(false);
}

bool AtaPioDriver::read_sector(uint64_t lba, uint8_t* buffer) {
    if (!drive_present_ || !buffer || lba >= sector_count_) return false;

    if (!poll_status(true)) return false;

    outb(ATA_DRIVE, ATA_DRIVE_MASTER | static_cast<uint8_t>((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO, static_cast<uint8_t>(lba & 0xFF));
    outb(ATA_LBA_MID, static_cast<uint8_t>((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, static_cast<uint8_t>((lba >> 16) & 0xFF));
    outb(ATA_CMD, ATA_CMD_READ_SECTORS);

    if (!wait_for_drq()) return false;

    for (int i = 0; i < 256; ++i) {
        uint16_t word = inw(ATA_DATA);
        buffer[i * 2] = static_cast<uint8_t>(word & 0xFF);
        buffer[i * 2 + 1] = static_cast<uint8_t>((word >> 8) & 0xFF);
    }

    return true;
}

bool AtaPioDriver::write_sector(uint64_t lba, const uint8_t* buffer) {
    if (!drive_present_ || !buffer || lba >= sector_count_) return false;

    if (!poll_status(true)) return false;

    outb(ATA_DRIVE, ATA_DRIVE_MASTER | static_cast<uint8_t>((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO, static_cast<uint8_t>(lba & 0xFF));
    outb(ATA_LBA_MID, static_cast<uint8_t>((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, static_cast<uint8_t>((lba >> 16) & 0xFF));
    outb(ATA_CMD, ATA_CMD_WRITE_SECTORS);

    if (!wait_for_drq()) return false;

    for (int i = 0; i < 256; ++i) {
        uint16_t word = static_cast<uint16_t>(buffer[i * 2]) |
                       (static_cast<uint16_t>(buffer[i * 2 + 1]) << 8);
        outw(ATA_DATA, word);
    }

    if (!poll_status(true)) return false;

    return true;
}

} // namespace block
} // namespace kernel
