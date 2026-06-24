#include <kernel/arch/interrupt_controller.hpp>
#include <kernel/arch/hal/io.hpp>

namespace arch {

#define GICD_BASE    ((volatile uint32_t*)0x8000000ULL)
#define GICR_BASE    ((volatile uint32_t*)0x80A0000ULL)
#define GICC_BASE    ((volatile uint32_t*)0x8010000ULL)

#define GICD_CTLR       0x000
#define GICD_TYPER      0x004
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800
#define GICD_IGROUPR    0x080
#define GICD_SGIR       0xF00

#define GICR_WAKER      0x0014
#define GICR_IGROUPR0   0x0080
#define GICR_ISENABLER0 0x0100
#define GICR_ICENABLER0 0x0180

#define GICC_CTLR       0x0000
#define GICC_PMR        0x0004
#define GICC_IAR        0x000C
#define GICC_EOIR       0x0010

static volatile uint32_t* gicd = GICD_BASE;
static volatile uint32_t* gicc = GICC_BASE;

void ArchInterruptController::init() {
    gicd[GICD_CTLR / 4] = 0;
    dsb_sy();

    gicd[GICD_ICENABLER / 4] = 0xFFFFFFFF;
    gicd[GICD_ICENABLER / 4 + 1] = 0xFFFFFFFF;
    dsb_sy();

    gicd[GICD_IGROUPR / 4] = 0xFFFFFFFF;
    gicd[GICD_IGROUPR / 4 + 1] = 0xFFFFFFFF;
    dsb_sy();

    gicc[GICC_CTLR / 4] = 0;
    dsb_sy();

    gicc[GICC_PMR / 4] = 0xFF;
    dsb_sy();

    gicd[GICD_CTLR / 4] = 1;
    dsb_sy();

    gicc[GICC_CTLR / 4] = 1;
    dsb_sy();
    isb();
}

void ArchInterruptController::eoi(uint8_t vector) {
    gicc[GICC_EOIR / 4] = vector;
    dsb_sy();
}

void ArchInterruptController::mask(uint8_t irq) {
    if (irq < 32) {
        gicd[GICD_ICENABLER / 4] = (1 << irq);
    } else {
        gicd[GICD_ICENABLER / 4 + (irq / 32)] = (1 << (irq % 32));
    }
    dsb_sy();
}

void ArchInterruptController::unmask(uint8_t irq) {
    if (irq < 32) {
        gicd[GICD_ISENABLER / 4] = (1 << irq);
    } else {
        gicd[GICD_ISENABLER / 4 + (irq / 32)] = (1 << (irq % 32));
    }
    dsb_sy();
}

IrqState ArchInterruptController::snapshot() {
    IrqState s;
    s.gic_mask = gicd[GICD_ISENABLER / 4];
    s.gic_mask |= (uint64_t)gicd[GICD_ISENABLER / 4 + 1] << 32;
    return s;
}

void ArchInterruptController::restore(const IrqState& state) {
    gicd[GICD_ICENABLER / 4] = 0xFFFFFFFF;
    gicd[GICD_ICENABLER / 4 + 1] = 0xFFFFFFFF;
    dsb_sy();
    gicd[GICD_ISENABLER / 4] = state.gic_mask & 0xFFFFFFFF;
    gicd[GICD_ISENABLER / 4 + 1] = (state.gic_mask >> 32) & 0xFFFFFFFF;
    dsb_sy();
}

} // namespace arch
