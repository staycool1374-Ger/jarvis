/// @file virtio.hpp
/// @brief Virtio PCI transport — modern (1.0) interface via PCI capabilities.

#pragma once

#include <types.hpp>
#include <kernel/arch/pci.hpp>
#include <kernel/memory/vmm.hpp>

namespace arch {

/// Virtio PCI vendor ID
constexpr uint16_t VIRTIO_PCI_VENDOR = 0x1AF4;

/// Virtio PCI device IDs (modern = 0x1040 + type)
constexpr uint16_t VIRTIO_DEVICE_BLOCK  = 0x1042; // modern virtio-blk
constexpr uint16_t VIRTIO_DEVICE_NET    = 0x1043; // modern virtio-net

/// Legacy device IDs
constexpr uint16_t VIRTIO_DEVICE_BLOCK_LEGACY = 0x1001;
constexpr uint16_t VIRTIO_DEVICE_NET_LEGACY   = 0x1000;

/// Virtio PCI capability types (cfg_type in virtio_pci_cap)
enum VirtioCfgType : uint8_t {
    VIRTIO_CFG_COMMON       = 1,
    VIRTIO_CFG_NOTIFY       = 2,
    VIRTIO_CFG_ISR          = 3,
    VIRTIO_CFG_DEVICE       = 4,
    VIRTIO_CFG_PCI_ACCESS   = 5,
};

/// Device status register bits
enum VirtioDeviceStatus : uint8_t {
    VIRTIO_STATUS_RESET         = 0x00,
    VIRTIO_STATUS_ACK           = 1,
    VIRTIO_STATUS_DRIVER        = 2,
    VIRTIO_STATUS_DRIVER_OK     = 4,
    VIRTIO_STATUS_FEATURES_OK   = 8,
    VIRTIO_STATUS_NEEDS_RESET   = 64,
    VIRTIO_STATUS_FAILED        = 128,
};

/// Virtio feature bits (common)
constexpr uint64_t VIRTIO_F_VERSION_1     = 1ULL << 32; // modern interface
constexpr uint64_t VIRTIO_F_RING_INDIRECT_DESC = 1ULL << 28;
constexpr uint64_t VIRTIO_F_RING_EVENT_IDX = 1ULL << 29;

/// Notify flags for kick
constexpr uint16_t VIRTIO_NOTIFY_NEXT    = 0; // kick next descriptor

/// Max descriptor ring size
constexpr uint16_t VIRTIO_QUEUE_SIZE_MAX = 1024;

/// Virtio descriptor flags
enum VirtioDescFlags : uint16_t {
    VIRTIO_DESC_F_NEXT     = 1,
    VIRTIO_DESC_F_WRITE    = 2,
    VIRTIO_DESC_F_INDIRECT = 4,
};

/// A single virtqueue descriptor (16 bytes)
struct VirtqDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

/// Available ring header
struct VirtqAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

/// Used ring entry
struct VirtqUsedElem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

/// Used ring header
struct VirtqUsed {
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[];
} __attribute__((packed));

/// Mapped MMIO region for a Virtio capability
struct VirtioMmio {
    uint64_t virt_addr;   // virtual address (HHDM-mapped)
    uint64_t phys_addr;   // physical address (from BAR + offset)
    uint32_t length;
};

/// Common configuration register offsets (via VIRTIO_CFG_COMMON)
enum VirtioCommonReg : uint16_t {
    VIRTIO_COMMON_DEVICE_FEATURE_SEL = 0x00,
    VIRTIO_COMMON_DEVICE_FEATURE     = 0x04,
    VIRTIO_COMMON_DRIVER_FEATURE_SEL = 0x08,
    VIRTIO_COMMON_DRIVER_FEATURE     = 0x0C,
    VIRTIO_COMMON_MSIX_CONFIG        = 0x10,
    VIRTIO_COMMON_NUM_QUEUES         = 0x12,
    VIRTIO_COMMON_DEVICE_STATUS      = 0x14,
    VIRTIO_COMMON_CONFIG_GENERATION  = 0x15,
    VIRTIO_COMMON_QUEUE_SEL          = 0x18,
    VIRTIO_COMMON_QUEUE_SIZE         = 0x1A,
    VIRTIO_COMMON_QUEUE_MSIX_VECTOR  = 0x1C,
    VIRTIO_COMMON_QUEUE_ENABLE       = 0x1E,
    VIRTIO_COMMON_QUEUE_NOTIFY_OFF   = 0x20,
    VIRTIO_COMMON_QUEUE_DESC_LO      = 0x22,
    VIRTIO_COMMON_QUEUE_DESC_HI      = 0x26,
    VIRTIO_COMMON_QUEUE_DRIVER_LO    = 0x28,
    VIRTIO_COMMON_QUEUE_DRIVER_HI    = 0x2C,
    VIRTIO_COMMON_QUEUE_DEVICE_LO    = 0x30,
    VIRTIO_COMMON_QUEUE_DEVICE_HI    = 0x34,
};

/// Notify register offsets (via VIRTIO_CFG_NOTIFY)
constexpr uint32_t VIRTIO_NOTIFY_OFF_MUL = 0; // notify_off_multiplier

/// Virtio PCI capability structure (in PCI config space)
struct VirtioPciCap {
    uint8_t  cap_vndr;
    uint8_t  cap_next;
    uint8_t  cap_len;
    uint8_t  cfg_type;
    uint8_t  bar;
    uint8_t  padding[3];
    uint32_t offset;
    uint32_t length;
} __attribute__((packed));

/// Parsed Virtio device transport information
struct VirtioTransport {
    PciBdf    bdf;
    uint16_t  device_id;
    bool      modern;             // modern (1.0) or legacy

    VirtioMmio common_cfg;        // VIRTIO_CFG_COMMON
    VirtioMmio notify_cfg;        // VIRTIO_CFG_NOTIFY
    VirtioMmio isr_cfg;           // VIRTIO_CFG_ISR
    VirtioMmio device_cfg;        // VIRTIO_CFG_DEVICE

    uint32_t notify_off_multiplier;
};

// --- Transport API ---

/// Check if a PCI device is a Virtio device (vendor 0x1AF4).
inline bool virtio_is_virtio_device(const PciDeviceInfo& info) {
    return info.vendor_id == VIRTIO_PCI_VENDOR;
}

/// Find the first Virtio device with the given device ID.
/// @return true if found, populates @p transport.
bool virtio_find_device(uint16_t device_id, VirtioTransport& transport);

/// Initialize Virtio transport: parse capabilities, reset device, negotiate
/// features, set up queues.
/// @return true on success.
bool virtio_init_transport(VirtioTransport& transport);

/// Map a BAR-based MMIO region for a Virtio capability.
bool virtio_map_mmio(uint8_t bar, uint32_t offset, uint32_t length,
                     VirtioMmio& mmio_out, PciBdf bdf);

/// Read a 32-bit register from the Virtio common config.
inline uint32_t virtio_read_common(const VirtioTransport& t, uint16_t reg) {
    auto* ptr = reinterpret_cast<volatile uint32_t*>(t.common_cfg.virt_addr + reg);
    return *ptr;
}

/// Write a 32-bit register to the Virtio common config.
inline void virtio_write_common(const VirtioTransport& t, uint16_t reg, uint32_t val) {
    auto* ptr = reinterpret_cast<volatile uint32_t*>(t.common_cfg.virt_addr + reg);
    *ptr = val;
}

/// Read device status.
inline uint8_t virtio_read_status(const VirtioTransport& t) {
    return static_cast<uint8_t>(virtio_read_common(t, VIRTIO_COMMON_DEVICE_STATUS));
}

/// Write device status.
inline void virtio_write_status(const VirtioTransport& t, uint8_t status) {
    virtio_write_common(t, VIRTIO_COMMON_DEVICE_STATUS, status);
}

/// Negotiate features: offer device features, accept driver features.
/// @return true if feature negotiation succeeded.
bool virtio_negotiate_features(VirtioTransport& t, uint64_t driver_features);

/// Set up a virtqueue.
/// @param t         Transport
/// @param queue_idx Queue index (0-based)
/// @param queue_size Number of descriptors (must be power of 2)
/// @param desc_phys Physical address of descriptor ring
/// @param avail_phys Physical address of available ring
/// @param used_phys Physical address of used ring
/// @return true on success
bool virtio_setup_queue(VirtioTransport& t, uint16_t queue_idx,
                        uint16_t queue_size, uint64_t desc_phys,
                        uint64_t avail_phys, uint64_t used_phys);

/// Notify the device that new descriptors are available.
inline void virtio_notify(const VirtioTransport& t, uint16_t queue_idx) {
    uint32_t off = t.notify_off_multiplier * queue_idx;
    auto* ptr = reinterpret_cast<volatile uint32_t*>(t.notify_cfg.virt_addr + off);
    *ptr = queue_idx;
}

} // namespace arch
