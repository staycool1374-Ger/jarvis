#pragma once

#include <types.hpp>
#include <kernel/arch/hal/io.hpp>

// NOTE: included inside namespace arch {} in pci.hpp — do not re-wrap

/// QEMU virt aarch64 ECAM base address (PCIe configuration space)
/// This is memory-mapped, not I/O port based like x86 CF8/CFC
#ifndef CONFIG_PCI_ECAM_BASE
#define CONFIG_PCI_ECAM_BASE 0x100000000ULL  // 4GB default
#endif

constexpr uint64_t PCI_ECAM_BASE = CONFIG_PCI_ECAM_BASE;
constexpr uint64_t PCI_ECAM_SIZE = 0x100000000ULL;  // 256 buses * 32 devices * 8 functions * 4KB = 256MB

/// Build ECAM address from BDF + register offset
/// ECAM layout: base + (bus << 20) | (device << 15) | (function << 12) | reg
inline uint64_t pci_ecam_addr(PciBdf bdf, uint8_t reg) {
    return PCI_ECAM_BASE
         | (static_cast<uint64_t>(bdf.bus)      << 20)
         | (static_cast<uint64_t>(bdf.device)    << 15)
         | (static_cast<uint64_t>(bdf.function)  << 12)
         | reg;
}

/// ECAM config space read (32-bit)
inline uint32_t pci_ecam_readl(PciBdf bdf, uint8_t reg) {
    volatile uint32_t* addr = reinterpret_cast<volatile uint32_t*>(pci_ecam_addr(bdf, reg));
    return *addr;
}

/// ECAM config space read (16-bit)
inline uint16_t pci_ecam_readw(PciBdf bdf, uint8_t reg) {
    volatile uint16_t* addr = reinterpret_cast<volatile uint16_t*>(pci_ecam_addr(bdf, reg));
    return *addr;
}

/// ECAM config space read (8-bit)
inline uint8_t pci_ecam_readb(PciBdf bdf, uint8_t reg) {
    volatile uint8_t* addr = reinterpret_cast<volatile uint8_t*>(pci_ecam_addr(bdf, reg));
    return *addr;
}

/// ECAM config space write (32-bit)
inline void pci_ecam_writel(PciBdf bdf, uint8_t reg, uint32_t val) {
    volatile uint32_t* addr = reinterpret_cast<volatile uint32_t*>(pci_ecam_addr(bdf, reg));
    *addr = val;
}

/// ECAM config space write (16-bit)
inline void pci_ecam_writew(PciBdf bdf, uint8_t reg, uint16_t val) {
    volatile uint16_t* addr = reinterpret_cast<volatile uint16_t*>(pci_ecam_addr(bdf, reg));
    *addr = val;
}

/// ECAM config space write (8-bit)
inline void pci_ecam_writeb(PciBdf bdf, uint8_t reg, uint8_t val) {
    volatile uint8_t* addr = reinterpret_cast<volatile uint8_t*>(pci_ecam_addr(bdf, reg));
    *addr = val;
}

/// Override vendor/device read for ECAM
inline uint16_t pci_read_vendor_ecam(PciBdf bdf) {
    return pci_ecam_readw(bdf, PCI_VENDOR_ID);
}

inline uint16_t pci_read_device_ecam(PciBdf bdf) {
    return pci_ecam_readw(bdf, PCI_DEVICE_ID);
}

inline bool pci_device_exists_ecam(PciBdf bdf) {
    return pci_read_vendor_ecam(bdf) != 0xFFFF;
}

/// ECAM-aware config space access (replaces CF8/CFC inline functions for aarch64)
inline uint32_t pci_config_readl_ecam(uint32_t addr) {
    // Not used for ECAM - kept for API compatibility
    (void)addr;
    return 0;
}

inline uint8_t pci_config_readb_ecam(uint32_t addr) {
    (void)addr;
    return 0;
}

inline void pci_config_writel_ecam(uint32_t addr, uint32_t val) {
    (void)addr;
    (void)val;
}

inline uint16_t pci_config_readw_ecam(uint32_t addr) {
    (void)addr;
    return 0;
}

/// ECAM-aware make_addr (kept for API compatibility, returns ECAM physical addr)
inline uint64_t pci_make_addr_ecam(PciBdf bdf, uint8_t reg) {
    return pci_ecam_addr(bdf, reg);
}