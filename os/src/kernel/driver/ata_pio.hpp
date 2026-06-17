#pragma once

#include <kernel/driver/block_device.hpp>

namespace kernel {
namespace block {

class AtaPioDriver final : public BlockDevice {
public:
    AtaPioDriver(uint16_t port_base = 0x1F0, uint8_t drive_head = 0xE0);
    ~AtaPioDriver() = default;

    bool init();

    bool read_sector(uint64_t lba, uint8_t* buffer) override;
    bool write_sector(uint64_t lba, const uint8_t* buffer) override;
    uint64_t sector_count() const override { return sector_count_; }
    uint64_t sector_size() const override { return BLOCK_SIZE; }
    bool is_read_only() const override { return false; }

    static AtaPioDriver* probe_first_drive();

private:
    bool poll_status(bool wait_for_bsy);
    bool wait_for_drq();
    bool identify();

    uint16_t port_base_;
    uint8_t  drive_head_;
    uint64_t sector_count_ = 0;
    bool     drive_present_ = false;
};

} // namespace block
} // namespace kernel
