#pragma once

#include <types.hpp>

namespace arch {

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

} // namespace arch

// ---------------------------------------------------------------------------
// Architecture-specific PCI config space access
//   x86_64: CF8/CFC port I/O (0xCF8 / 0xCFC)
//   aarch64 / riscv64: ECAM memory-mapped (QEMU virt at 0x100000000)
//
// All access functions work on dword-aligned addresses internally, matching
// the x86 CF8/CFC semantics where bits [1:0] of the register offset select
// the byte/word within the dword.  This ensures identical behavior across
// all architectures.
// ---------------------------------------------------------------------------

#if defined(CONFIG_ARCH_X86_64)

#include <kernel/arch/hal/io.hpp>

namespace arch {

constexpr uint16_t PCI_CONFIG_ADDR = 0xCF8;
constexpr uint16_t PCI_CONFIG_DATA = 0xCFC;

inline uint64_t pci_make_addr(PciBdf bdf, uint8_t reg) {
    return 0x80000000U
         | (static_cast<uint32_t>(bdf.bus)      << 16)
         | (static_cast<uint32_t>(bdf.device)    << 11)
         | (static_cast<uint32_t>(bdf.function)  << 8)
         | reg;
}

inline uint32_t pci_config_readl(uint64_t addr) {
    outl(PCI_CONFIG_ADDR, static_cast<uint32_t>(addr & ~3ULL));
    return inl(PCI_CONFIG_DATA);
}

inline uint8_t pci_config_readb(uint64_t addr) {
    uint32_t val = pci_config_readl(addr);
    return static_cast<uint8_t>((val >> ((addr & 3) * 8)) & 0xFF);
}

inline uint16_t pci_config_readw(uint64_t addr) {
    uint32_t val = pci_config_readl(addr);
    return static_cast<uint16_t>((val >> ((addr & 2) * 8)) & 0xFFFF);
}

inline void pci_config_writel(uint64_t addr, uint32_t val) {
    outl(PCI_CONFIG_ADDR, static_cast<uint32_t>(addr & ~3ULL));
    outl(PCI_CONFIG_DATA, val);
}

inline void pci_config_writew(uint64_t addr, uint16_t val) {
    uint32_t old = pci_config_readl(addr);
    uint32_t shift = (addr & 2) * 8;
    uint32_t new_val = (old & ~(0xFFFF << shift))
                     | (static_cast<uint32_t>(val) << shift);
    pci_config_writel(addr, new_val);
}

inline void pci_config_writeb(uint64_t addr, uint8_t val) {
    uint32_t old = pci_config_readl(addr);
    uint32_t shift = (addr & 3) * 8;
    uint32_t new_val = (old & ~(0xFF << shift))
                     | (static_cast<uint32_t>(val) << shift);
    pci_config_writel(addr, new_val);
}

} // namespace arch

#elif defined(CONFIG_ARCH_AARCH64) || defined(CONFIG_ARCH_RISCV64)

#include <kernel/arch/hal/io.hpp>

namespace arch {

#ifndef CONFIG_PCI_ECAM_BASE
#define CONFIG_PCI_ECAM_BASE 0x100000000ULL
#endif
constexpr uint64_t PCI_ECAM_BASE = CONFIG_PCI_ECAM_BASE;

inline uint64_t pci_make_addr(PciBdf bdf, uint8_t reg) {
    return PCI_ECAM_BASE
         | (static_cast<uint64_t>(bdf.bus)      << 20)
         | (static_cast<uint64_t>(bdf.device)    << 15)
         | (static_cast<uint64_t>(bdf.function)  << 12)
         | reg;
}

inline uint32_t pci_config_readl(uint64_t addr) {
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(addr & ~3ULL);
    return *ptr;
}

inline uint8_t pci_config_readb(uint64_t addr) {
    uint32_t val = pci_config_readl(addr);
    return static_cast<uint8_t>((val >> ((addr & 3) * 8)) & 0xFF);
}

inline uint16_t pci_config_readw(uint64_t addr) {
    uint32_t val = pci_config_readl(addr);
    return static_cast<uint16_t>((val >> ((addr & 2) * 8)) & 0xFFFF);
}

inline void pci_config_writel(uint64_t addr, uint32_t val) {
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(addr & ~3ULL);
    *ptr = val;
}

inline void pci_config_writew(uint64_t addr, uint16_t val) {
    uint32_t old = pci_config_readl(addr);
    uint32_t shift = (addr & 2) * 8;
    uint32_t new_val = (old & ~(0xFFFF << shift))
                     | (static_cast<uint32_t>(val) << shift);
    pci_config_writel(addr, new_val);
}

inline void pci_config_writeb(uint64_t addr, uint8_t val) {
    uint32_t old = pci_config_readl(addr);
    uint32_t shift = (addr & 3) * 8;
    uint32_t new_val = (old & ~(0xFF << shift))
                     | (static_cast<uint32_t>(val) << shift);
    pci_config_writel(addr, new_val);
}

} // namespace arch

#else
#error "pci.hpp: unsupported architecture"
#endif
