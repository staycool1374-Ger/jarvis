#pragma once

#include <types.hpp>
#include <kernel/arch/hal/io.hpp>

/// @brief RISC-V 64 PLIC (Platform-Level Interrupt Controller) register
/// definitions and accessors.
///
/// Memory map (QEMU virt):
///   PLIC_BASE     = 0x0C00_0000
///   Priority      = base + 0x0000_0000  (one 4-byte word per source)
///   Pending       = base + 0x0000_1000
///   Enable        = base + 0x0000_2000  (one bit per source)
///   Threshold     = base + 0x0020_0000
///   Claim/Complete= base + 0x0020_0004
///
/// IRQ map (QEMU virt):
///   0   = reserved
///   1-8 = virtio-mmio devices
///   9   = (unused)
///   10  = UART
///   11  = (unused)
///   12  = keyboard (PS/2)
namespace arch {

inline constexpr uint64_t PLIC_BASE     = 0x0C000000ULL;
inline constexpr uint64_t PLIC_PRIORITY = PLIC_BASE + 0x000000;
inline constexpr uint64_t PLIC_PENDING  = PLIC_BASE + 0x001000;
inline constexpr uint64_t PLIC_ENABLE   = PLIC_BASE + 0x002000;
inline constexpr uint64_t PLIC_THRESHOLD = PLIC_BASE + 0x200000;
inline constexpr uint64_t PLIC_CLAIM    = PLIC_BASE + 0x200004;

/// @brief QEMU virt IRQ numbers.
inline constexpr uint32_t IRQ_VIRTIO0 = 1;
inline constexpr uint32_t IRQ_VIRTIO1 = 2;
inline constexpr uint32_t IRQ_VIRTIO2 = 3;
inline constexpr uint32_t IRQ_VIRTIO3 = 4;
inline constexpr uint32_t IRQ_VIRTIO4 = 5;
inline constexpr uint32_t IRQ_UART    = 10;
inline constexpr uint32_t IRQ_KEYBOARD = 12;

// ─── Accessors ───────────────────────────────────────────────────────────
inline volatile uint32_t *plic_priority_reg(uint32_t irq) {
    return reinterpret_cast<volatile uint32_t *>(PLIC_PRIORITY + irq * 4);
}

inline volatile uint32_t *plic_enable_reg(uint32_t irq) {
    return reinterpret_cast<volatile uint32_t *>(PLIC_ENABLE + (irq / 32) * 4);
}

inline uint32_t plic_claim() {
    return *reinterpret_cast<volatile uint32_t *>(PLIC_CLAIM);
}

inline void plic_complete(uint32_t intid) {
    *reinterpret_cast<volatile uint32_t *>(PLIC_CLAIM) = intid;
}

} // namespace arch
