#include <kernel/arch/x86_64/hal/apic.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/cpuid.hpp>
#include <kernel/arch/msr.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/address.hpp>
#include <logger.hpp>

namespace arch {

// ─── Static members ───────────────────────────────────────────────────────
APIC::Mode APIC::mode_          = MODE_NONE;
bool        APIC::enabled_      = false;
bool        APIC::timer_active_ = false;
uint32_t    APIC::bus_freq_hz_  = 0;
uint32_t    APIC::tsc_deadline_supported_ = 0;

// ─── CPUID caps ──────────────────────────────────────────────────────────
struct Caps { bool apic, x2apic, tsc_deadline; };
static Caps caps() {
    Caps c{};
    auto r = cpuid(1);
    c.apic         = (r.edx >> 9) & 1;
    c.x2apic       = (r.ecx >> 21) & 1;
    c.tsc_deadline = (r.ecx >> 24) & 1;
    return c;
}

// ─── MMIO helpers ────────────────────────────────────────────────────────
static constexpr uint64_t LAPIC_PHYS  = 0xFEE00000;
static constexpr uint64_t IOAPIC_PHYS = 0xFEC00000;

static volatile uint32_t *ioapic_ptr(uint32_t reg) {
    return reinterpret_cast<volatile uint32_t *>(
        arch::HHDM_OFFSET + IOAPIC_PHYS + reg);
}

static void ioapic_wr(uint32_t sel, uint32_t v) {
    *ioapic_ptr(0x00) = sel;        // IOREGSEL
    *ioapic_ptr(0x10) = v;          // IOWIN
}

// ─── Public API ───────────────────────────────────────────────────────────
// The local APIC is NOT enabled at boot — we only set up MMIO mappings and
// I/O APIC structures.  This keeps the legacy PIC fully operational while
// the APIC infrastructure is prepared for a future hot-switch.
// When the APIC timer (TSC-deadline or periodic) is ready, a call to
// apic_enable() will program MSR_APIC_BASE, the SPURIOUS vector, LVT
// entries, and unmask the appropriate I/O APIC redirections.

bool APIC::is_apic_supported() { return caps().apic; }

bool APIC::map_mmio() {
    auto map = [](uint64_t phys) {
        kernel::VMM::map_page(arch::HHDM_OFFSET + phys, phys, false);
    };
    map(LAPIC_PHYS);
    map(IOAPIC_PHYS);
    return true;
}

bool APIC::init() {
    auto c = caps();
    if (!c.apic) return false;

    tsc_deadline_supported_ = c.tsc_deadline;

    // I/O APIC: mask all legacy IRQs (PIC remains active)
    auto redir = [](uint8_t irq, uint8_t vec, bool mask) {
        uint32_t entry = vec;
        if (mask) entry |= IOAPIC_MASKED;
        uint32_t r = IOAPIC_REDIR + irq * 2;
        ioapic_wr(r,     entry);
        ioapic_wr(r + 1, 0);
    };
    for (int i = 0; i < 16; ++i)
        redir(i, 32 + i, true);

    kernel::Logger::info("APIC: mapped, I/O APIC masked (PIC active)");
    return true;
}

void APIC::eoi() {
    // No-op until APIC is enabled
}

// ─── APIC timer stubs (inactive until apic_enable) ────────────────────────
void APIC::timer_init(uint32_t frequency_hz) { (void)frequency_hz; }
void APIC::timer_start() {}
void APIC::timer_stop() {}
uint32_t APIC::timer_current_count() { return 0; }
uint32_t APIC::calibrate_bus_hz() { return 0; }

} // namespace arch
