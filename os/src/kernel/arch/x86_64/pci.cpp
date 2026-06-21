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

static const char* pci_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Legacy";
        case 0x01: return "Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Comm";
        case 0x08: return "Peripheral";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "SerialBus";
        case 0x0D: return "Wireless";
        case 0x0E: return "I2O";
        case 0x0F: return "Satellite";
        case 0x10: return "Encrypt";
        case 0x11: return "SignalProc";
        default:   return "Unknown";
    }
}

static const char* pci_subclass_name(uint8_t class_code, uint8_t subclass) {
    if (class_code == 0x01) {
        switch (subclass) {
            case 0x00: return "SCSI";
            case 0x01: return "IDE";
            case 0x02: return "Floppy";
            case 0x03: return "IPI";
            case 0x04: return "RAID";
            case 0x05: return "ATA";
            case 0x06: return "SATA";
            case 0x07: return "SAS";
            case 0x08: return "NVM";
            case 0x09: return "UFS";
            default:   return "Other";
        }
    }
    if (class_code == 0x02) {
        switch (subclass) {
            case 0x00: return "Ethernet";
            case 0x01: return "TokenRing";
            case 0x02: return "FDDI";
            case 0x03: return "ATM";
            case 0x04: return "ISDN";
            case 0x05: return "WorldFip";
            case 0x06: return "PICMG";
            case 0x07: return "Infiniband";
            case 0x08: return "Fabric";
            default:   return "Other";
        }
    }
    if (class_code == 0x03) {
        switch (subclass) {
            case 0x00: return "VGA";
            case 0x01: return "XGA";
            case 0x02: return "3D";
            default:   return "Other";
        }
    }
    if (class_code == 0x06) {
        switch (subclass) {
            case 0x00: return "HostBridge";
            case 0x01: return "ISA";
            case 0x02: return "EISA";
            case 0x03: return "MCABridge";
            case 0x04: return "PCI2PCI";
            case 0x05: return "PCMCIA";
            case 0x06: return "NuBus";
            case 0x07: return "CardBus";
            case 0x08: return "RACEway";
            case 0x09: return "PCI2PCI(Transparent)";
            case 0x0A: return "InfiniLink";
            default:   return "Other";
        }
    }
    if (class_code == 0x0C) {
        switch (subclass) {
            case 0x00: return "FireWire";
            case 0x01: return "ACCESSbus";
            case 0x02: return "SSA";
            case 0x03: return "USB";
            case 0x04: return "FibreChannel";
            case 0x05: return "SMBus";
            case 0x06: return "InfiniBand";
            case 0x07: return "IPMI";
            case 0x08: return "SERCOS";
            case 0x09: return "CANbus";
            default:   return "Other";
        }
    }
    return "";
}

} // anonymous namespace

namespace arch {

size_t pci_scan_all() {
    g_device_count = 0;
    probe_bus(0);
    Logger::info("PCI: scanned, %d devices found", g_device_count);
#ifdef CONFIG_DEBUG
    pci_dump_tree();
#endif
    return g_device_count;
}

void pci_dump_tree() {
    uint8_t current_bus = 0xFF;
    for (size_t i = 0; i < g_device_count; ++i) {
        const auto& d = g_devices[i];
        if (d.bdf.bus != current_bus) {
            current_bus = d.bdf.bus;
            Logger::raw_write("PCI Bus ");
            Logger::print_dec(d.bdf.bus);
            Logger::raw_write(":\n");
        }
        Logger::raw_write("  ");
        Logger::print_dec(d.bdf.bus);
        Logger::raw_write(":");
        Logger::print_dec(d.bdf.device);
        Logger::raw_write(".");
        Logger::print_dec(d.bdf.function);
        Logger::raw_write("  [");
        Logger::print_hex(d.vendor_id);
        Logger::raw_write(":");
        Logger::print_hex(d.device_id);
        Logger::raw_write("]  ");
        const char* cn = pci_class_name(d.class_code);
        Logger::raw_write(cn);
        Logger::raw_write(" (");
        Logger::print_hex(d.class_code);
        Logger::raw_write("/");
        Logger::print_hex(d.subclass);
        const char* sn = pci_subclass_name(d.class_code, d.subclass);
        if (sn[0]) {
            Logger::raw_write(" ");
            Logger::raw_write(sn);
        }
        Logger::raw_write(")\n");
    }
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
    // Zero out all BARs first
    for (uint8_t i = 0; i < 6; ++i) info.bars[i] = {};
    info.bar_count = 0;

    for (uint8_t i = 0; i < 6; ++i) {
        uint32_t reg = PCI_BAR0 + i * 4;
        uint32_t raw = pci_config_readl(
            pci_make_addr(info.bdf, static_cast<uint8_t>(reg)));

        if (raw == 0) continue;

        PciBar& bar = info.bars[i];
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
        ++info.bar_count;
    }
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

// --- MSI vector allocator ---
// Vectors available for MSI/MSI-X: 48-127, 129-255.
// (0-31 = CPU exceptions, 32-47 = PIC IRQs, 0x80 = syscall)
static bool g_vector_used[256] = {};
static bool g_vector_init = false;

static void init_vector_alloc() {
    if (g_vector_init) return;
    for (int i = 0; i < 32; ++i) g_vector_used[i] = true;       // CPU exceptions
    for (int i = 32; i < 48; ++i) g_vector_used[i] = true;       // PIC IRQs
    g_vector_used[0x80] = true;                                   // SYSCALL
    g_vector_init = true;
}

uint8_t pci_alloc_vector() {
    init_vector_alloc();
    for (uint16_t v = 48; v < 256; ++v) {
        if (v == 0x80) continue;
        if (!g_vector_used[v]) {
            g_vector_used[v] = true;
            return static_cast<uint8_t>(v);
        }
    }
    return 0;
}

void pci_free_vector(uint8_t vec) {
    if (vec < 48 || vec == 0x80) return;
    g_vector_used[vec] = false;
}

// --- Capability list walking ---

uint8_t pci_find_capability(PciBdf bdf, uint8_t cap_id) {
    // Check if the device supports capabilities
    uint16_t status = pci_config_readw(pci_make_addr(bdf, PCI_STATUS));
    if (!(status & PCI_STATUS_CAP_LIST)) return 0;

    uint8_t offset = pci_config_readb(pci_make_addr(bdf, PCI_CAP_PTR));
    while (offset != 0) {
        uint8_t id = pci_config_readb(pci_make_addr(bdf, offset));
        if (id == cap_id) return offset;
        offset = pci_config_readb(pci_make_addr(bdf, offset + 1));
    }
    return 0;
}

// --- MSI enable ---

uint8_t pci_enable_msi(PciBdf bdf, uint8_t apic_id) {
    uint8_t cap = pci_find_capability(bdf, PCI_CAP_ID_MSI);
    if (cap == 0) return 0;

    uint16_t ctrl = pci_config_readw(pci_make_addr(bdf, cap + 2));
    bool is_64 = (ctrl & PCI_MSI_CTRL_64BIT) != 0;

    uint8_t vec = pci_alloc_vector();
    if (vec == 0) return 0;

    // Message Address Register
    uint32_t addr = PCI_MSI_ADDR_BASE | (static_cast<uint32_t>(apic_id) << 12);
    pci_config_writel(pci_make_addr(bdf, cap + 4), addr);

    // Message Data Register
    uint16_t data = PCI_MSI_DATA_FIXED | vec;
    uint8_t data_off = is_64 ? static_cast<uint8_t>(cap + 12)
                             : static_cast<uint8_t>(cap + 8);
    pci_config_writel(pci_make_addr(bdf, data_off), data);

    // Upper Address (64-bit only)
    if (is_64) {
        pci_config_writel(pci_make_addr(bdf, cap + 8), 0);
    }

    // Enable MSI, set MME = 0 (single message)
    ctrl = (ctrl & ~PCI_MSI_CTRL_MME_MASK) | PCI_MSI_CTRL_ENABLE;
    pci_config_writel(pci_make_addr(bdf, cap + 2), ctrl);

    Logger::info("MSI: enabled on %d:%d.%d vector=%d",
        bdf.bus, bdf.device, bdf.function, vec);
    return vec;
}

// --- MSI-X enable ---

uint8_t pci_enable_msix(PciBdf bdf, uint16_t entry, uint8_t apic_id) {
    uint8_t cap = pci_find_capability(bdf, PCI_CAP_ID_MSIX);
    if (cap == 0) return 0;

    // Read table offset and BIR
    uint32_t tbl_reg = pci_config_readl(pci_make_addr(bdf, cap + 4));
    uint8_t  bir = tbl_reg & 0x7;
    uint32_t tbl_offset = tbl_reg & ~0x7;

    // Read the device's BAR to get the MMIO base
    PciDeviceInfo info = pci_read_device_info(bdf);
    if (bir >= 6 || info.bars[bir].address == 0) return 0;
    uint64_t mmio_base = info.bars[bir].address;

    uint8_t vec = pci_alloc_vector();
    if (vec == 0) return 0;

    // MSI-X table entry layout in MMIO (each entry = 16 bytes):
    //   +0: Message Address (lower 32 bits)
    //   +4: Message Address (upper 32 bits)
    //   +8: Message Data (lower 32 bits)
    //   +12: Vector Control (32 bits, bit 0 = mask)
    //
    // NOTE: We write to the table via memory-mapped I/O. The PCI config
    // space write helpers are for config space only; for MMIO we need
    // a direct write to the mapped address.
    uint64_t entry_addr = mmio_base + tbl_offset + static_cast<uint64_t>(entry) * 16;

    // We have to map this MMIO region if not already mapped. For now,
    // we assume the kernel has direct access via HHDM (identity mapping
    // covering all physical memory starting at some base virtual addr).
    //
    // On this kernel, physical memory is mapped at HHDM_BASE.
    // We use a simple volatile write through the physical address directly
    // since QEMU's PC platform maps PCI MMIO in the physical address space
    // below 4GB, and the kernel's page tables identity-map the first 4GB.
    volatile uint32_t* tbl = reinterpret_cast<volatile uint32_t*>(entry_addr);

    uint32_t addr_low  = PCI_MSI_ADDR_BASE | (static_cast<uint32_t>(apic_id) << 12);
    uint32_t addr_high = 0;
    uint32_t msg_data  = PCI_MSI_DATA_FIXED | vec;

    tbl[0] = addr_low;   // Message Address low
    tbl[1] = addr_high;  // Message Address high
    tbl[2] = msg_data;   // Message Data
    tbl[3] = 0;          // Vector Control (unmasked)

    // Enable MSI-X and unmask function
    uint16_t ctrl = pci_config_readw(pci_make_addr(bdf, cap + 2));
    ctrl |= PCI_MSIX_CTRL_ENABLE;
    ctrl &= ~PCI_MSIX_CTRL_FUNCMASK;
    pci_config_writel(pci_make_addr(bdf, cap + 2), ctrl);

    Logger::info("MSI-X: enabled on %d:%d.%d entry=%d vector=%d",
        bdf.bus, bdf.device, bdf.function, entry, vec);
    return vec;
}

} // namespace arch
