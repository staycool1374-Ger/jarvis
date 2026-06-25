#include <kernel/arch/interrupt_controller.hpp>
#include <kernel/arch/hal/io.hpp>
#include <kernel/arch/idt.hpp>

namespace arch {

// GIC memory map (QEMU virt)
#define GICD_BASE      (0x8000000ULL)
#define GICC_BASE      (0x8010000ULL)   // GICv2 CPU interface
#define GICR_RD_BASE   (0x80A0000ULL)   // GICv3 RD frame for core 0
#define GICR_SGI_BASE  (0x80B0000ULL)   // GICv3 SGI/PPI frame for core 0

// Distributor (GICD) register offsets
#define GICD_CTLR      0x0000
#define GICD_TYPER     0x0004
#define GICD_IGROUPR   0x0080
#define GICD_ISENABLER 0x0100
#define GICD_ICENABLER 0x0180

// Redistributor RD frame offsets
#define GICR_WAKER     0x0014

// Redistributor SGI/PPI frame offsets
#define GICR_IGROUPR0  0x0080
#define GICR_ISENABLER0 0x0100
#define GICR_ICENABLER0 0x0180

// GICC (GICv2 CPU interface) offsets
#define GICC_CTLR      0x0000
#define GICC_PMR       0x0004
#define GICC_IAR       0x000C
#define GICC_EOIR      0x0010

static bool gic_is_v3 = false;

void ArchInterruptController::init() {
    volatile uint32_t* gicd = reinterpret_cast<volatile uint32_t*>(GICD_BASE);
    volatile uint32_t* gicc = reinterpret_cast<volatile uint32_t*>(GICC_BASE);

    // Detect GIC version from GICD_TYPER
    uint32_t typer = gicd[GICD_TYPER / 4];
    uint32_t arch_ver = (typer >> 11) & 0x1F;
    gic_is_v3 = (arch_ver >= 3);

    // === Distributor init ===
    gicd[GICD_CTLR / 4] = 0;
    arch::dsb_sy();

    gicd[GICD_ICENABLER / 4] = 0xFFFFFFFF;
    gicd[GICD_ICENABLER / 4 + 1] = 0xFFFFFFFF;
    arch::dsb_sy();

    gicd[GICD_IGROUPR / 4] = 0xFFFFFFFF;
    gicd[GICD_IGROUPR / 4 + 1] = 0xFFFFFFFF;
    arch::dsb_sy();

    if (gic_is_v3) {
        // === GICv3: system registers + redistributor ===
        volatile uint32_t* gicr_rd = reinterpret_cast<volatile uint32_t*>(GICR_RD_BASE);
        volatile uint32_t* gicr_sgi = reinterpret_cast<volatile uint32_t*>(GICR_SGI_BASE);

        // Wake redistributor
        uint32_t waker = gicr_rd[GICR_WAKER / 4];
        waker &= ~(1U << 0);
        gicr_rd[GICR_WAKER / 4] = waker;
        arch::dsb_sy();
        int timeout = 1000000;
        while ((gicr_rd[GICR_WAKER / 4] & (1U << 1)) && --timeout > 0) {
            arch::dsb_sy();
        }

        // Set PPIs 16-31 to Group 1
        gicr_sgi[GICR_IGROUPR0 / 4] |= 0xFFFF0000U;
        arch::dsb_sy();

        // Enable system register access for CPU interface
        uint64_t sre;
        asm volatile("mrs %0, ICC_SRE_EL1" : "=r"(sre));
        sre |= 1;
        asm volatile("msr ICC_SRE_EL1, %0" : : "r"(sre));
        arch::dsb_sy();

        // Set priority mask
        asm volatile("msr ICC_PMR_EL1, %0" : : "r"(0xFFUL));
        arch::dsb_sy();

        // Enable Group 1
        asm volatile("msr ICC_IGRPEN1_EL1, %0" : : "r"(1UL));
        arch::dsb_sy();
        arch::isb();

        // Enable Timer PPI 30
        gicr_sgi[GICR_ISENABLER0 / 4] = (1U << 30);
        arch::dsb_sy();
    } else {
        // === GICv2: memory-mapped CPU interface ===
        gicc[GICC_CTLR / 4] = 0;
        arch::dsb_sy();

        gicc[GICC_PMR / 4] = 0xFF;
        arch::dsb_sy();

        // Enable Timer PPI 30 through distributor
        gicd[GICD_ISENABLER / 4] = (1U << 30);
        arch::dsb_sy();
    }

    // Enable distributor
    gicd[GICD_CTLR / 4] = 1;
    arch::dsb_sy();

    if (!gic_is_v3) {
        // Enable GICv2 CPU interface
        gicc[GICC_CTLR / 4] = 1;
        arch::dsb_sy();
    }
    arch::isb();
}

void ArchInterruptController::eoi(uint8_t vector) {
    if (gic_is_v3) {
        asm volatile("msr ICC_EOIR1_EL1, %0" : : "r"((uint64_t)vector));
    } else {
        volatile uint32_t* gicc = reinterpret_cast<volatile uint32_t*>(GICC_BASE);
        gicc[GICC_EOIR / 4] = vector;
    }
    arch::dsb_sy();
}

void ArchInterruptController::mask(uint8_t irq) {
    volatile uint32_t* gicd = reinterpret_cast<volatile uint32_t*>(GICD_BASE);
    if (gic_is_v3 && irq < 32) {
        volatile uint32_t* gicr_sgi = reinterpret_cast<volatile uint32_t*>(GICR_SGI_BASE);
        gicr_sgi[GICR_ICENABLER0 / 4] = (1U << irq);
    } else {
        gicd[GICD_ICENABLER / 4 + (irq / 32)] = (1U << (irq % 32));
    }
    arch::dsb_sy();
}

void ArchInterruptController::unmask(uint8_t irq) {
    volatile uint32_t* gicd = reinterpret_cast<volatile uint32_t*>(GICD_BASE);
    if (gic_is_v3 && irq < 32) {
        volatile uint32_t* gicr_sgi = reinterpret_cast<volatile uint32_t*>(GICR_SGI_BASE);
        gicr_sgi[GICR_ISENABLER0 / 4] = (1U << irq);
    } else {
        gicd[GICD_ISENABLER / 4 + (irq / 32)] = (1U << (irq % 32));
    }
    arch::dsb_sy();
}

IrqState ArchInterruptController::snapshot() {
    IrqState s = {};
    volatile uint32_t* gicd = reinterpret_cast<volatile uint32_t*>(GICD_BASE);
    s.gic_mask = gicd[GICD_ISENABLER / 4];
    s.gic_mask |= (uint64_t)gicd[GICD_ISENABLER / 4 + 1] << 32;
    return s;
}

void ArchInterruptController::restore(const IrqState& state) {
    volatile uint32_t* gicd = reinterpret_cast<volatile uint32_t*>(GICD_BASE);
    gicd[GICD_ICENABLER / 4] = 0xFFFFFFFF;
    gicd[GICD_ICENABLER / 4 + 1] = 0xFFFFFFFF;
    arch::dsb_sy();
    gicd[GICD_ISENABLER / 4] = state.gic_mask & 0xFFFFFFFF;
    gicd[GICD_ISENABLER / 4 + 1] = (state.gic_mask >> 32) & 0xFFFFFFFF;
    arch::dsb_sy();
}

/// Main IRQ handler — called from vectors.S IRQ entries.
/// Handles both GICv2 and GICv3 IAR/EOIR.
extern "C" void handle_gic_irq(void) {
    uint64_t intid;

    if (gic_is_v3) {
        asm volatile("mrs %0, ICC_IAR1_EL1" : "=r"(intid));
    } else {
        volatile uint32_t* gicc = reinterpret_cast<volatile uint32_t*>(GICC_BASE);
        intid = gicc[GICC_IAR / 4];
    }

    if (intid >= 1023) goto ack;  // spurious

    if (intid == 30) {
        uint64_t elr;
        asm volatile("mrs %0, elr_el1" : "=r"(elr));
        IDT::handle_interrupt(static_cast<uint64_t>(InterruptVector::TIMER), 0, elr);
    }
    // Other INTIDs are ignored for now

ack:
    if (gic_is_v3) {
        asm volatile("msr ICC_EOIR1_EL1, %0" : : "r"(intid));
    } else {
        volatile uint32_t* gicc = reinterpret_cast<volatile uint32_t*>(GICC_BASE);
        gicc[GICC_EOIR / 4] = static_cast<uint32_t>(intid);
    }
}

} // namespace arch
