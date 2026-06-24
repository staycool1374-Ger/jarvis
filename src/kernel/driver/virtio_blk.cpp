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

/// @file virtio_blk.cpp
/// @brief Virtio block driver implementation.

#if defined(CONFIG_ARCH_X86_64)

#include <kernel/driver/virtio_blk.hpp>
#include <kernel/memory/pmm.hpp>
#include <logger.hpp>
#include <string.hpp>

using namespace kernel;
using namespace arch;

namespace kernel::block {

VirtioBlkDriver::VirtioBlkDriver(arch::VirtioTransport& transport)
    : transport_(transport) {
}

VirtioBlkDriver::~VirtioBlkDriver() {
    if (desc_phys_)  PMM::free_page(desc_phys_);
    if (avail_phys_) PMM::free_page(avail_phys_);
    if (used_phys_)  PMM::free_page(used_phys_);
    if (dma_buf_phys_) PMM::free_page(dma_buf_phys_);
}

bool VirtioBlkDriver::init() {
    if (!arch::virtio_init_transport(transport_)) {
        Logger::error("virtio-blk: transport init failed");
        return false;
    }

    // Negotiate features: require VERSION_1, offer nothing extra
    uint64_t features = VIRTIO_F_VERSION_1;
    if (!arch::virtio_negotiate_features(transport_, features)) {
        return false;
    }

    // Determine queue size
    queue_size_ = 16; // minimal for block I/O

    // Allocate physically contiguous pages for queue memory
    // Descriptor ring: queue_size * 16 bytes (16 per desc)
    // Available ring: 6 + queue_size * 2 bytes
    // Used ring: 8 + queue_size * 8 bytes
    // Total: roughly < 4KB for small queues, use one page each
    desc_phys_  = PMM::alloc_page();
    avail_phys_ = PMM::alloc_page();
    used_phys_  = PMM::alloc_page();
    dma_buf_phys_ = PMM::alloc_page();

    if (!desc_phys_ || !avail_phys_ || !used_phys_ || !dma_buf_phys_) {
        Logger::error("virtio-blk: OOM for queue memory");
        return false;
    }

    desc_    = reinterpret_cast<arch::VirtqDesc*>(arch::HHDM_OFFSET + desc_phys_);
    avail_   = reinterpret_cast<arch::VirtqAvail*>(arch::HHDM_OFFSET + avail_phys_);
    used_    = reinterpret_cast<arch::VirtqUsed*>(arch::HHDM_OFFSET + used_phys_);
    dma_buf_ = reinterpret_cast<uint8_t*>(arch::HHDM_OFFSET + dma_buf_phys_);

    memset(desc_, 0, PAGE_SIZE);
    memset(avail_, 0, PAGE_SIZE);
    memset(used_, 0, PAGE_SIZE);
    memset(dma_buf_, 0, PAGE_SIZE);

    if (!arch::virtio_setup_queue(transport_, 0, queue_size_,
                                  desc_phys_, avail_phys_, used_phys_)) {
        Logger::error("virtio-blk: queue setup failed");
        return false;
    }

    // Set DRIVER_OK
    uint8_t status = arch::virtio_read_status(transport_);
    arch::virtio_write_status(transport_, status | VIRTIO_STATUS_DRIVER_OK);

    // Read sector count from device config (at offset 0 for virtio-blk)
    auto* cfg = reinterpret_cast<volatile uint64_t*>(transport_.device_cfg.virt_addr);
    sector_count_ = cfg[0];
    Logger::info("virtio-blk: %d sectors (%d MB)", sector_count_,
                 sector_count_ * BLOCK_SIZE / (1024 * 1024));

    return true;
}

bool VirtioBlkDriver::submit_request(uint32_t type, uint64_t sector,
                                     uint8_t* data, bool is_read) {
    if (!desc_ || !avail_ || !used_) return false;

    // Build the request header in the DMA buffer
    auto* hdr = reinterpret_cast<VirtioBlkReqHdr*>(dma_buf_);
    hdr->type     = type;
    hdr->reserved = 0;
    hdr->sector   = sector;

    // Data goes after header (16 bytes) in the DMA buffer
    uint8_t* data_area = dma_buf_ + sizeof(VirtioBlkReqHdr);
    if (!is_read) {
        memcpy(data_area, data, BLOCK_SIZE);
    }

    // Build descriptor chain:
    // desc[0]: request header (driver write → device read)
    // desc[1]: data (driver read/write → device write/read)
    // desc[2]: status byte (device write → driver read)

    uint16_t idx = avail_idx_ % queue_size_;
    desc_[idx].addr  = dma_buf_phys_;
    desc_[idx].len   = sizeof(VirtioBlkReqHdr);
    desc_[idx].flags = VIRTIO_DESC_F_NEXT;
    desc_[idx].next  = static_cast<uint16_t>((idx + 1) % queue_size_);

    uint16_t data_idx = static_cast<uint16_t>((idx + 1) % queue_size_);
    desc_[data_idx].addr  = dma_buf_phys_ + sizeof(VirtioBlkReqHdr);
    desc_[data_idx].len   = BLOCK_SIZE;
    desc_[data_idx].flags = VIRTIO_DESC_F_NEXT |
                           (is_read ? VIRTIO_DESC_F_WRITE : 0);
    desc_[data_idx].next  = static_cast<uint16_t>((idx + 2) % queue_size_);

    uint16_t status_idx = static_cast<uint16_t>((idx + 2) % queue_size_);
    desc_[status_idx].addr  = dma_buf_phys_ + sizeof(VirtioBlkReqHdr) + BLOCK_SIZE;
    desc_[status_idx].len   = 1;
    desc_[status_idx].flags = VIRTIO_DESC_F_WRITE;
    desc_[status_idx].next  = 0;

    // Place in available ring
    avail_->ring[avail_->idx % queue_size_] = idx;
    __sync_synchronize();
    avail_->idx = static_cast<uint16_t>(avail_->idx + 1);
    __sync_synchronize();

    // Kick the device
    arch::virtio_notify(transport_, 0);

    // Poll for completion (busy wait — acceptable for block I/O)
    uint16_t used_idx = used_->idx;
    int timeout = 1000000;
    while (used_->idx == used_idx && --timeout > 0) {
        __sync_synchronize();
    }
    if (timeout <= 0) {
        Logger::error("virtio-blk: request timeout");
        return false;
    }

    // Check status
    auto* status_ptr = reinterpret_cast<volatile uint8_t*>(
        dma_buf_ + sizeof(VirtioBlkReqHdr) + BLOCK_SIZE);
    if (*status_ptr != VIRTIO_BLK_S_OK) {
        Logger::error("virtio-blk: request failed (status=%d)", *status_ptr);
        return false;
    }

    // Copy data back for reads
    if (is_read) {
        memcpy(data, data_area, BLOCK_SIZE);
    }

    avail_idx_ = static_cast<uint16_t>(avail_idx_ + 1);
    return true;
}

bool VirtioBlkDriver::read_sector(uint64_t lba, uint8_t* buffer) {
    return submit_request(VIRTIO_BLK_T_IN, lba, buffer, true);
}

bool VirtioBlkDriver::write_sector(uint64_t lba, const uint8_t* buffer) {
    // Need to cast away const — the DMA buffer is writable
    return submit_request(VIRTIO_BLK_T_OUT, lba,
                          const_cast<uint8_t*>(buffer), false);
}

VirtioBlkDriver* VirtioBlkDriver::probe() {
    arch::VirtioTransport transport;
    if (!arch::virtio_find_device(VIRTIO_DEVICE_BLOCK, transport)) {
        Logger::info("virtio-blk: no device found");
        return nullptr;
    }

    auto* drv = new VirtioBlkDriver(transport);
    if (!drv->init()) {
        delete drv;
        return nullptr;
    }
    return drv;
}

} // namespace kernel::block

#endif // CONFIG_ARCH_X86_64
