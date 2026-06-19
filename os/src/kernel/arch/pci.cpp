/// @file pci.cpp
/// @brief PCI bus enumeration using CF8/CFC config space access.

#include <kernel/arch/pci.hpp>
#include <logger.hpp>

using namespace kernel;

namespace {

/// Fixed-size buffer for discovered devices.
arch::PciDeviceInfo g_devices[arch::PCI_MAX_DEVICES_FOUND];
size_t g_device_count = 0;

/// Probe a single BDF; if present, read its info and store it.
void probe_bdf(arch::PciBdf bdf) {
    if (!arch::pci_device_exists(bdf)) return;
    if (g_device_count >= arch::PCI_MAX_DEVICES_FOUND) {
        Logger::warn("PCI: device buffer full, stopping scan");
        return;
    }
    g_devices[g_device_count] = arch::pci_read_device_info(bdf);
    ++g_device_count;
}

/// Probe all functions of a device, if it is multi-function.
void probe_device(arch::PciBdf bdf) {
    probe_bdf(bdf);
    arch::PciBdf func_bdf = bdf;
    for (uint8_t fn = 1; fn < arch::PCI_MAX_FUNCTIONS; ++fn) {
        func_bdf.function = fn;
        if (arch::pci_device_exists(func_bdf)) {
            probe_bdf(func_bdf);
        }
    }
}

/// Probe all devices on a given bus.
void probe_bus(uint8_t bus) {
    arch::PciBdf bdf;
    bdf.bus = bus;
    bdf.function = 0;
    for (uint8_t dev = 0; dev < arch::PCI_MAX_DEVICES; ++dev) {
        bdf.device = dev;
        arch::PciBdf bdf0 = bdf;
        if (!arch::pci_device_exists(bdf0)) continue;

        uint8_t header_type = arch::pci_config_readb(
            arch::pci_make_addr(bdf0, arch::PCI_HEADER_TYPE));

        if (header_type & arch::PCI_HEADER_TYPE_MULTI) {
            probe_device(bdf0);
        } else {
            probe_bdf(bdf0);
        }

        // If this is a PCI-to-PCI bridge, scan the secondary bus.
        uint8_t class_reg = arch::pci_config_readb(
            arch::pci_make_addr(bdf0, arch::PCI_CLASS));
        uint8_t subclass_reg = arch::pci_config_readb(
            arch::pci_make_addr(bdf0, arch::PCI_SUBCLASS));
        if (class_reg == 0x06 && subclass_reg == 0x04) {
            uint8_t secondary = arch::pci_config_readb(
                arch::pci_make_addr(bdf0, arch::PCI_SECONDARY_BUS));
            probe_bus(secondary);
        }
    }
}

} // anonymous namespace

namespace arch {

size_t pci_scan_all() {
    g_device_count = 0;
    probe_bus(0);
    Logger::info("PCI: scanned, %d devices found", g_device_count);
    return g_device_count;
}

const PciDeviceInfo* pci_devices() {
    return g_devices;
}

size_t pci_device_count() {
    return g_device_count;
}

const PciDeviceInfo* pci_find_device(uint8_t class_code, uint8_t subclass) {
    for (size_t i = 0; i < g_device_count; ++i) {
        if (g_devices[i].class_code == class_code &&
            g_devices[i].subclass == subclass) {
            return &g_devices[i];
        }
    }
    return nullptr;
}

void pci_parse_bars(PciDeviceInfo& info) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        uint32_t reg = PCI_BAR0 + i * 4;
        uint32_t raw = pci_config_readl(
            pci_make_addr(info.bdf, static_cast<uint8_t>(reg)));

        if (raw == 0) continue;

        PciBar& bar = info.bars[count];
        if (raw & 1) {
            // I/O BAR
            bar.type = PciBarType::IO;
            bar.address = static_cast<uint64_t>(raw & ~3);
            bar.prefetchable = false;
            // Size probe: write all 1s, read back, restore
            pci_config_writel(pci_make_addr(info.bdf, static_cast<uint8_t>(reg)), 0xFFFFFFFF);
            uint32_t mask = pci_config_readl(
                pci_make_addr(info.bdf, static_cast<uint8_t>(reg)));
            pci_config_writel(pci_make_addr(info.bdf, static_cast<uint8_t>(reg)), raw);
            bar.size = static_cast<uint64_t>(~(mask & 0xFFFFFFFC)) + 1;
        } else {
            // Memory BAR
            bool is_64 = (raw & 6) == 4;
            bar.prefetchable = (raw & 8) != 0;
            bar.type = is_64 ? PciBarType::MEMORY_64 : PciBarType::MEMORY_32;

            // Size probe
            pci_config_writel(pci_make_addr(info.bdf, static_cast<uint8_t>(reg)), 0xFFFFFFFF);
            uint32_t mask_low = pci_config_readl(
                pci_make_addr(info.bdf, static_cast<uint8_t>(reg)));
            pci_config_writel(pci_make_addr(info.bdf, static_cast<uint8_t>(reg)), raw);

            if (is_64 && i < 5) {
                // Read high 32 bits for 64-bit BAR
                uint32_t addr_high = pci_config_readl(
                    pci_make_addr(info.bdf, static_cast<uint8_t>(reg + 4)));
                bar.address = (static_cast<uint64_t>(addr_high) << 32)
                            | (raw & 0xFFFFFFF0);
                uint64_t size_mask = static_cast<uint64_t>(mask_low) & 0xFFFFFFF0;
                bar.size = (~size_mask) + 1;
                ++i; // skip next BAR register
            } else {
                bar.address = static_cast<uint64_t>(raw & 0xFFFFFFF0);
                bar.size = static_cast<uint64_t>(~(mask_low & 0xFFFFFFF0)) + 1;
            }
        }
        ++count;
    }
    info.bar_count = count;
}

PciDeviceInfo pci_read_device_info(PciBdf bdf) {
    PciDeviceInfo info = {};
    info.bdf = bdf;
    info.vendor_id   = pci_read_vendor(bdf);
    info.device_id   = pci_read_device(bdf);
    uint32_t addr_base = pci_make_addr(bdf, 0);
    info.revision    = pci_config_readb(addr_base + PCI_REVISION);
    info.prog_if     = pci_config_readb(addr_base + PCI_PROG_IF);
    info.subclass    = pci_config_readb(addr_base + PCI_SUBCLASS);
    info.class_code  = pci_config_readb(addr_base + PCI_CLASS);
    info.header_type = pci_config_readb(addr_base + PCI_HEADER_TYPE);
    pci_parse_bars(info);
    return info;
}

} // namespace arch
