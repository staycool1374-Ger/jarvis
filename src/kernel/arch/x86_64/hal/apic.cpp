#include <kernel/arch/x86_64/hal/apic.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/cpuid.hpp>
#include <kernel/arch/msr.hpp>
#include <kernel/arch/timer.hpp>
#include <logger.hpp>

namespace arch {

// ─── Static members ───────────────────────────────────────────────────────
uint32_t APIC::bus_freq_hz_ = 0;
bool APIC::enabled_ = false;

// ─── x2APIC MSR base — each APIC register at MSR (0x800 + reg_offset / 16) ─
static inline uint32_t x2apic_msr(uint32_t mmio_offset) {
    return 0x800 + (mmio_offset >> 4);
}

// ─── Helper: check CPUID for APIC / x2APIC / TSC-deadline support ─────────
struct ApicCaps {
    bool apic;
    bool x2apic;
    bool tsc_deadline;
};

static ApicCaps apic_caps() {
    ApicCaps caps{};
    auto r = cpuid(1);
    caps.apic         = (r.edx >> 9) & 1;
    caps.x2apic       = (r.ecx >> 21) & 1;
    caps.tsc_deadline = (r.ecx >> 24) & 1;
    return caps;
}

// ─── x2APIC register helpers ──────────────────────────────────────────────
// x2APIC accesses all local APIC registers via MSR[0x800..0x8FF], requiring
// no MMIO page mappings.  The MSR address = 0x800 + (MMIO_offset >> 4).

static void x2_write(uint32_t mmio_offset, uint32_t value) {
    wrmsr(x2apic_msr(mmio_offset), value);
}

static uint32_t x2_read(uint32_t mmio_offset) {
    return static_cast<uint32_t>(rdmsr(x2apic_msr(mmio_offset)));
}

// ─── Public API ───────────────────────────────────────────────────────────
// This implementation requires x2APIC (MSR-based) access.  CPUs that lack
// x2APIC are uncommon on modern QEMU/hardware; if absent we simply keep the
// legacy PIC/PIT and skip APIC init entirely.

bool APIC::is_apic_supported() {
    return apic_caps().apic;
}

bool APIC::init() {
    auto caps = apic_caps();
    if (!caps.apic) {
        kernel::Logger::warn("APIC not supported by CPU");
        enabled_ = false;
        return false;
    }
    if (!caps.x2apic) {
        kernel::Logger::warn("CPU lacks x2APIC — keeping legacy PIC");
        enabled_ = false;
        return false;
    }

    uint64_t apic_base = rdmsr(MSR_APIC_BASE);
    apic_base |= (APIC_BASE_ENABLE | APIC_BASE_X2APIC);
    wrmsr(MSR_APIC_BASE, apic_base);

    x2_write(REG_SPURIOUS, SPURIOUS_VECTOR | SPURIOUS_ENABLE);

    x2_write(REG_LVT_TIMER,   LVT_MASKED);
    x2_write(REG_LVT_THERMAL, LVT_MASKED);
    x2_write(REG_LVT_PERFMON, LVT_MASKED);
    x2_write(REG_LVT_LINT0,   LVT_MASKED);
    x2_write(REG_LVT_LINT1,   LVT_MASKED);
    x2_write(REG_LVT_ERROR,   LVT_MASKED);

    x2_write(REG_ESR, 0);
    x2_read(REG_ESR);

    x2_write(REG_TPR, 0);

    enabled_ = true;
    kernel::Logger::info("x2APIC initialised (TSC-deadline=%d)", caps.tsc_deadline);
    return true;
}

void APIC::eoi() {
    if (!enabled_)
        return;
    wrmsr(x2apic_msr(REG_EOI), 0);
}

// ─── APIC timer (TSC-deadline mode preferred) ────────────────────────────

void APIC::timer_init(uint32_t frequency_hz) {
    (void)frequency_hz;
    auto caps = apic_caps();

    if (caps.tsc_deadline) {
        x2_write(REG_LVT_TIMER, APIC_TIMER_VECTOR | LVT_TIMER_TSCDEADLINE);
    } else {
        // Fallback: periodic mode (requires I/O APIC I/O-routing — not wired)
        kernel::Logger::warn("APIC timer: TSC-deadline not available, keeping PIT");
        x2_write(REG_LVT_TIMER, LVT_MASKED);
    }
}

void APIC::timer_start() {
    auto caps = apic_caps();
    if (!caps.tsc_deadline)
        return;

    uint64_t tsc_freq = arch::Timer::tsc_freq_hz();
    if (tsc_freq == 0) {
        kernel::Logger::warn("APIC timer: TSC freq unknown, using 2 GHz");
        tsc_freq = 2000000000ULL;
    }

    uint64_t tsc_now = rdtsc();
    uint64_t delta = tsc_freq / 1000;  // 1000 Hz
    wrmsr(MSR_TSC_DEADLINE, tsc_now + (delta > 0 ? delta : 1));
}

void APIC::timer_stop() {
    wrmsr(MSR_TSC_DEADLINE, 0);
    x2_write(REG_LVT_TIMER, LVT_MASKED);
}

uint32_t APIC::timer_current_count() {
    return x2_read(REG_TIMER_CURCNT);
}

// ─── Calibration ──────────────────────────────────────────────────────────
// (Unused with TSC-deadline mode; kept for periodic-mode fallback.)

uint32_t APIC::calibrate_timer_hz(uint32_t target_hz) {
    (void)target_hz;
    return 0;
}

} // namespace arch
