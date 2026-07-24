#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>
#include <kernel/arch/msr.hpp>

namespace arch {

/// @brief x86_64 APIC driver — Local APIC, I/O APIC, and APIC timer.
/// All MMIO registers are accessed via their physical page, identity-mapped
/// during early boot. This driver is device-only — no SMP support yet.
class APIC {
public:
    /// @brief Initialise the local APIC.
    /// Requires x2APIC support (MSR-based access).
    /// Returns true if the APIC was successfully enabled.
    static bool init();

    /// @brief Send End-Of-Interrupt (no-op if APIC not initialised).
    static void eoi();

    /// @brief Whether the APIC was successfully initialised.
    static bool is_enabled() { return enabled_; }

    /// @brief Initialise the APIC timer in periodic mode.
    /// @param frequency_hz Desired tick frequency.
    static void timer_init(uint32_t frequency_hz);

    /// @brief Start the APIC timer (after timer_init calibrates).
    static void timer_start();

    /// @brief Stop the APIC timer.
    static void timer_stop();

    /// @brief Read the current APIC timer count (decrements toward 0).
    static uint32_t timer_current_count();

    /// @brief Check whether the LOCAL APIC is supported (CPUID.01H:EDX[9]).
    static bool is_apic_supported();

    /// @brief APIC timer vector (reuses the same slot as PIT IRQ0).
    static constexpr uint8_t APIC_TIMER_VECTOR = 32;

private:
    // ─── Local APIC MMIO registers (offset from base) ──────────────────────
    static constexpr uint32_t APIC_BASE = 0xFEE00000;

    static constexpr uint32_t REG_ID           = 0x020;
    static constexpr uint32_t REG_VERSION      = 0x030;
    static constexpr uint32_t REG_TPR          = 0x080;
    static constexpr uint32_t REG_APR          = 0x090;
    static constexpr uint32_t REG_PPR          = 0x0A0;
    static constexpr uint32_t REG_EOI          = 0x0B0;
    static constexpr uint32_t REG_LDR          = 0x0D0;
    static constexpr uint32_t REG_DFR          = 0x0E0;
    static constexpr uint32_t REG_SPURIOUS     = 0x0F0;
    static constexpr uint32_t REG_ESR          = 0x280;
    static constexpr uint32_t REG_ICR_LOW      = 0x300;
    static constexpr uint32_t REG_ICR_HIGH     = 0x310;
    static constexpr uint32_t REG_LVT_TIMER    = 0x320;
    static constexpr uint32_t REG_LVT_THERMAL  = 0x330;
    static constexpr uint32_t REG_LVT_PERFMON  = 0x340;
    static constexpr uint32_t REG_LVT_LINT0    = 0x350;
    static constexpr uint32_t REG_LVT_LINT1    = 0x360;
    static constexpr uint32_t REG_LVT_ERROR    = 0x370;
    static constexpr uint32_t REG_TIMER_DIVIDE = 0x3E0;
    static constexpr uint32_t REG_TIMER_INITCNT = 0x380;
    static constexpr uint32_t REG_TIMER_CURCNT = 0x390;

    // ─── I/O APIC MMIO registers ───────────────────────────────────────────
    static constexpr uint32_t IOAPIC_BASE      = 0xFEC00000;
    static constexpr uint32_t IOAPIC_REG_SELECT = 0x00;
    static constexpr uint32_t IOAPIC_REG_DATA   = 0x10;
    static constexpr uint32_t IOAPIC_ID        = 0x00;
    static constexpr uint32_t IOAPIC_VERSION   = 0x01;
    static constexpr uint32_t IOAPIC_ARB       = 0x02;
    static constexpr uint32_t IOAPIC_REDIR_BASE = 0x10;
    static constexpr uint32_t IOAPIC_REDIR_SIZE = 0x02;

    // ─── MSR addresses ─────────────────────────────────────────────────────
    static constexpr uint32_t MSR_APIC_BASE     = 0x1B;
    static constexpr uint32_t MSR_TSC_DEADLINE  = 0x6E0;

    // ─── Bit flags ─────────────────────────────────────────────────────────
    static constexpr uint32_t APIC_BASE_ENABLE  = 0x800;
    static constexpr uint32_t APIC_BASE_X2APIC  = 0x400;
    static constexpr uint32_t LVT_MASKED        = 0x10000;
    static constexpr uint32_t LVT_TIMER_PERIODIC = 0x20000;
    static constexpr uint32_t LVT_TIMER_TSCDEADLINE = 0x40000;
    static constexpr uint32_t SPURIOUS_ENABLE   = 0x100;
    static constexpr uint32_t SPURIOUS_VECTOR   = 0xFF;
    static constexpr uint32_t TIMER_DIVIDE_1    = 0xB;  // divide = 1
    static constexpr uint32_t DELIVERY_FIXED    = 0;
    static constexpr uint32_t DELIVERY_LOWPRI   = 0x100;
    static constexpr uint32_t DEST_MODE_PHYSICAL = 0;
    static constexpr uint32_t TRIGGER_EDGE      = 0;
    static constexpr uint32_t TRIGGER_LEVEL     = 0x8000;
    static constexpr uint32_t MASKED            = 0x10000;
    static constexpr uint32_t DEST_ALL_EXCL_SELF = 0x400000;

    // ─── APIC register accessors ────────────────────────────────────────────
    static volatile uint32_t *reg_ptr(uint32_t offset);
    static void reg_write(uint32_t offset, uint32_t value);
    static uint32_t reg_read(uint32_t offset);

    // ─── I/O APIC accessors ────────────────────────────────────────────────
    static void ioapic_write(uint32_t reg, uint32_t value);
    static uint32_t ioapic_read(uint32_t reg);
    static void ioapic_redirect_irq(uint8_t irq, uint8_t vector, uint32_t flags);
    static void ioapic_mask_irq(uint8_t irq);

    // ─── Calibration ───────────────────────────────────────────────────────
    static uint32_t calibrate_timer_hz(uint32_t target_hz);
    static uint32_t bus_freq_hz_;
    static bool enabled_;
};

} // namespace arch
