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

#include <kernel/driver/block_device.hpp>
#include <kernel/memory/pmm.hpp>
#include <constants.hpp>
#include <string.hpp>

namespace kernel {
namespace block {

MockBlockDevice::MockBlockDevice(uint64_t sector_count_)
    : sector_count_(sector_count_), read_only_(false), owns_data_(true)
{
    uint64_t bytes = sector_count_ * BLOCK_SIZE;
    uint64_t pages = (bytes + arch::PAGE_SIZE - 1) / arch::PAGE_SIZE;
    uint64_t phys = PMM::alloc_contiguous(pages);
    if (phys) {
        data_ = reinterpret_cast<uint8_t*>(phys + arch::HHDM_OFFSET);
        memset(data_, 0, bytes);
    }
}

MockBlockDevice::MockBlockDevice(const uint8_t* external_data,
                                  uint64_t sector_count_, bool read_only)
    : sector_count_(sector_count_), read_only_(read_only), owns_data_(false)
{
    data_ = const_cast<uint8_t*>(external_data);
}

MockBlockDevice::~MockBlockDevice() {
    if (owns_data_ && data_) {
        uint64_t bytes = sector_count_ * BLOCK_SIZE;
        uint64_t pages = (bytes + arch::PAGE_SIZE - 1) / arch::PAGE_SIZE;
        uint64_t phys = reinterpret_cast<uint64_t>(data_) - arch::HHDM_OFFSET;
        for (uint64_t i = 0; i < pages; ++i) {
            PMM::free_page(phys + i * arch::PAGE_SIZE);
        }
    }
}

bool MockBlockDevice::read_sector(uint64_t lba, uint8_t* buffer) {
    if (!buffer || !data_ || lba >= sector_count_) return false;
    memcpy(buffer, data_ + lba * BLOCK_SIZE, BLOCK_SIZE);
    return true;
}

bool MockBlockDevice::write_sector(uint64_t lba, const uint8_t* buffer) {
    if (!buffer || !data_ || lba >= sector_count_ || read_only_) return false;
    memcpy(data_ + lba * BLOCK_SIZE, buffer, BLOCK_SIZE);
    return true;
}

} // namespace block
} // namespace kernel
