/// @file pci.hpp
/// @brief PCI enumeration — CF8/CFC config space access and bus scan.

#pragma once

#include <types.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/pci_errors.hpp>

namespace arch {

/// PCI configuration space I/O ports
constexpr uint16_t PCI_CONFIG_ADDR = 0xCF8;
constexpr uint16_t PCI_CONFIG_DATA = 0xCFC;

/// Max devices per bus
constexpr uint8_t  PCI_MAX_DEVICES   = 32;
constexpr uint8_t  PCI_MAX_FUNCTIONS = 8;
constexpr uint8_t  PCI_MAX_BUSES     = 256;

/// Standard PCI config space register offsets
enum PciRegister : uint8_t {
    PCI_VENDOR_ID       = 0x00,
    PCI_DEVICE_ID       = 0x02,
    PCI_COMMAND         = 0x04,
    PCI_STATUS          = 0x06,
    PCI_REVISION        = 0x08,
    PCI_PROG_IF         = 0x09,
    PCI_SUBCLASS        = 0x0A,
    PCI_CLASS           = 0x0B,
    PCI_HEADER_TYPE     = 0x0E,
    PCI_BIST            = 0x0F,
    PCI_BAR0            = 0x10,
    PCI_BAR1            = 0x14,
    PCI_BAR2            = 0x18,
    PCI_BAR3            = 0x1C,
    PCI_BAR4            = 0x20,
    PCI_BAR5            = 0x24,
    PCI_SECONDARY_BUS   = 0x19,
};

/// PCI command register bits
enum PciCommand : uint16_t {
    PCI_CMD_IO_SPACE    = 1 << 0,
    PCI_CMD_MEM_SPACE   = 1 << 1,
    PCI_CMD_BUS_MASTER  = 1 << 2,
};

/// PCI header type flags
constexpr uint8_t PCI_HEADER_TYPE_DEVICE  = 0x00;
constexpr uint8_t PCI_HEADER_TYPE_BRIDGE  = 0x01;
constexpr uint8_t PCI_HEADER_TYPE_MULTI   = 0x80;

/// BAR type
enum class PciBarType : uint8_t {
    MEMORY_32 = 0,
    IO        = 1,
    MEMORY_64 = 2,
};

/// Parsed BAR descriptor
struct PciBar {
    uint64_t    address;
    uint64_t    size;
    PciBarType  type;
    bool        prefetchable;
};

/// BDF (Bus:Device.Function) address
struct PciBdf {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    bool operator==(const PciBdf& o) const {
        return bus == o.bus && device == o.device && function == o.function;
    }
    bool operator!=(const PciBdf& o) const { return !(*this == o); }
};

/// Discovered device info
struct PciDeviceInfo {
    PciBdf  bdf;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    PciBar   bars[6];
    uint8_t  bar_count;
};

// --- Inline config space access helpers ---

inline void pci_config_addr(uint32_t addr) {
    outl(PCI_CONFIG_ADDR, addr);
}

inline uint32_t pci_config_readl(uint32_t addr) {
    pci_config_addr(addr);
    return inl(PCI_CONFIG_DATA);
}

inline uint8_t pci_config_readb(uint32_t addr) {
    uint32_t val = pci_config_readl(addr & ~3);
    return static_cast<uint8_t>((val >> ((addr & 3) * 8)) & 0xFF);
}

inline void pci_config_writel(uint32_t addr, uint32_t val) {
    pci_config_addr(addr);
    outl(PCI_CONFIG_DATA, val);
}

inline uint16_t pci_config_readw(uint32_t addr) {
    uint32_t val = pci_config_readl(addr & ~3);
    return static_cast<uint16_t>((val >> ((addr & 2) * 8)) & 0xFFFF);
}

/// Build a config space address from BDF + register offset
inline uint32_t pci_make_addr(PciBdf bdf, uint8_t reg) {
    return 0x80000000U
         | (static_cast<uint32_t>(bdf.bus)      << 16)
         | (static_cast<uint32_t>(bdf.device)    << 11)
         | (static_cast<uint32_t>(bdf.function)  << 8)
         | (reg & 0xFC);
}

/// Read vendor ID (return 0xFFFF if no device)
inline uint16_t pci_read_vendor(PciBdf bdf) {
    return pci_config_readw(pci_make_addr(bdf, PCI_VENDOR_ID));
}

/// Read device ID
inline uint16_t pci_read_device(PciBdf bdf) {
    return pci_config_readw(pci_make_addr(bdf, PCI_DEVICE_ID));
}

/// Check if a BDF has a valid device
inline bool pci_device_exists(PciBdf bdf) {
    return pci_read_vendor(bdf) != 0xFFFF;
}

// --- Enumeration ---

constexpr size_t PCI_MAX_DEVICES_FOUND = 128;

size_t pci_scan_all();
const PciDeviceInfo* pci_devices();
size_t pci_device_count();
const PciDeviceInfo* pci_find_device(uint8_t class_code, uint8_t subclass);
void pci_parse_bars(PciDeviceInfo& info);
PciDeviceInfo pci_read_device_info(PciBdf bdf);

} // namespace arch
