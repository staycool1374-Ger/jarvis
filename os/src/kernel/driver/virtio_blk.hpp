/// @file virtio_blk.hpp
/// @brief Virtio block driver — modern (Virtio 1.0) PCI transport.

#pragma once

#include <types.hpp>
#include <kernel/arch/virtio.hpp>
#include <kernel/driver/block_device.hpp>

namespace kernel::block {

/// Virtio block I/O request types
enum VirtioBlkReqType : uint32_t {
    VIRTIO_BLK_T_IN    = 0,
    VIRTIO_BLK_T_OUT   = 1,
    VIRTIO_BLK_T_FLUSH = 4,
};

/// Virtio block request header (must be 16-byte aligned)
struct VirtioBlkReqHdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

/// Virtio block response status
enum VirtioBlkStatus : uint8_t {
    VIRTIO_BLK_S_OK    = 0,
    VIRTIO_BLK_S_IOERR = 1,
    VIRTIO_BLK_S_UNSUP = 2,
};

/// Virtio block device driver (modern interface)
class VirtioBlkDriver final : public BlockDevice {
public:
    VirtioBlkDriver(arch::VirtioTransport& transport);
    ~VirtioBlkDriver();

    bool init();
    bool read_sector(uint64_t lba, uint8_t* buffer) override;
    bool write_sector(uint64_t lba, const uint8_t* buffer) override;
    uint64_t sector_count() const override { return sector_count_; }
    uint64_t sector_size() const override { return BLOCK_SIZE; }
    bool is_read_only() const override { return false; }

    static VirtioBlkDriver* probe();

private:
    bool submit_request(uint32_t type, uint64_t sector,
                        uint8_t* data, bool is_read);

    arch::VirtioTransport transport_;
    uint64_t sector_count_ = 0;

    // Queue memory (physically contiguous)
    uint64_t desc_phys_  = 0;
    uint64_t avail_phys_ = 0;
    uint64_t used_phys_  = 0;

    // Virtual addresses for queue memory
    arch::VirtqDesc*   desc_   = nullptr;
    arch::VirtqAvail*  avail_  = nullptr;
    arch::VirtqUsed*   used_   = nullptr;
    uint16_t     queue_size_ = 0;
    uint16_t     avail_idx_  = 0;

    // DMA buffer physical address
    uint64_t     dma_buf_phys_ = 0;
    uint8_t*     dma_buf_      = nullptr;
};

} // namespace kernel::block
