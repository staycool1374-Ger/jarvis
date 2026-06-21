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
