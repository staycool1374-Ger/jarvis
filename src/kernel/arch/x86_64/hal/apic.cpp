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

// x2APIC MSR base: each APIC register at MSR (0x800 + mmio_offset/16)
static inline uint32_t x2apic_msr(uint32_t mmio_offset) {
    return 0x800 + (mmio_offset >> 4);
}

// I/O APIC constants (mirrored from APIC class to keep file‑static helpers)
static constexpr uint32_t IOAPIC_REDIR_BASE  = 0x10;
static constexpr uint32_t IOAPIC_MASKED_BIT  = 0x10000;

static volatile uint32_t *lapic_ptr(uint32_t off) {
    return reinterpret_cast<volatile uint32_t *>(
        arch::HHDM_OFFSET + LAPIC_PHYS + off);
}

static void lapic_wr(uint32_t off, uint32_t v) { *lapic_ptr(off) = v; }
static uint32_t lapic_rd(uint32_t off) { return *lapic_ptr(off); }

static volatile uint32_t *ioapic_ptr(uint32_t reg) {
    return reinterpret_cast<volatile uint32_t *>(
        arch::HHDM_OFFSET + IOAPIC_PHYS + reg);
}

static void ioapic_wr(uint32_t sel, uint32_t v) {
    *ioapic_ptr(0x00) = sel;
    *ioapic_ptr(0x10) = v;
}

static void ioapic_redirect(uint8_t irq, uint8_t vector, bool masked) {
    uint32_t entry = vector;
    if (masked) entry |= IOAPIC_MASKED_BIT;
    ioapic_wr(IOAPIC_REDIR_BASE + irq * 2,     entry);
    ioapic_wr(IOAPIC_REDIR_BASE + irq * 2 + 1, 0);
}

// ─── Public API ───────────────────────────────────────────────────────────

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
    if (!c.apic) {
        kernel::Logger::warn("APIC: not supported");
        return false;
    }

    tsc_deadline_supported_ = c.tsc_deadline;

    // ── 1. Enable the local APIC ──────────────────────────────────────────
    // Try x2APIC (MSR) first, fall back to xAPIC (MMIO).
    bool x2 = c.x2apic;
    uint64_t base = rdmsr(MSR_APIC_BASE);
    base |= APIC_BASE_ENABLE;
    if (x2) base |= APIC_BASE_X2APIC;
    else    base &= ~APIC_BASE_X2APIC;
    wrmsr(MSR_APIC_BASE, base);
    mode_ = x2 ? MODE_X2 : MODE_XAPIC;

    auto wr = [&](uint32_t off, uint32_t v) {
        if (x2) wrmsr(x2apic_msr(off), v);
        else    lapic_wr(off, v);
    };
    auto rd = [&](uint32_t off) -> uint32_t {
        if (x2) return static_cast<uint32_t>(rdmsr(x2apic_msr(off)));
        else    return lapic_rd(off);
    };

    // ── 2. Spurious vector + enable ──────────────────────────────────────
    wr(REG_SPURIOUS, SPURIOUS_VECTOR | SPURIOUS_ENABLE);

    // ── 3. Mask all LVT entries ──────────────────────────────────────────
    wr(REG_LVT_TIMER,   LVT_MASKED);
    wr(REG_LVT_THERMAL, LVT_MASKED);
    wr(REG_LVT_PERFMON, LVT_MASKED);
    wr(REG_LVT_LINT0,   LVT_MASKED);
    wr(REG_LVT_LINT1,   LVT_MASKED);
    wr(REG_LVT_ERROR,   LVT_MASKED);

    // ── 4. Clear error status ────────────────────────────────────────────
    wr(REG_ESR, 0);
    rd(REG_ESR);

    // ── 5. TPR = 0 (accept all interrupt priorities) ─────────────────────
    wr(REG_TPR, 0);

    // ── 6. I/O APIC: route legacy IRQs ───────────────────────────────────
    arch::ioapic_redirect(0, 32, false);
    arch::ioapic_redirect(1, 33, false);
    for (int i = 2; i < 16; ++i)
        arch::ioapic_redirect(i, 32 + i, true);

    enabled_ = true;

    kernel::Logger::info("APIC: %s mode enabled, I/O APIC routing IRQ0→32 IRQ1→33",
                         x2 ? "x2APIC" : "xAPIC");
    return true;
}

void APIC::eoi() {
    if (!enabled_) return;
    if (mode_ == MODE_X2)
        wrmsr(x2apic_msr(REG_EOI), 0);
    else
        lapic_wr(REG_EOI, 0);
}

// ─── APIC timer stubs ────────────────────────────────────────────────────
void APIC::timer_init(uint32_t frequency_hz) { (void)frequency_hz; }
void APIC::timer_start() {}
void APIC::timer_stop() {}
uint32_t APIC::timer_current_count() { return 0; }
uint32_t APIC::calibrate_bus_hz() { return 0; }

} // namespace arch
