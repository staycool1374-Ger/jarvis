# Test Cases — v0.4.1 (Phase 5: APIC, SMP Boot & IRQ Priority)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Per-CPU Data Isolation — test_percpu.cpp
- Each CPU has isolated GDT with unique base/limit
- Each CPU has isolated TSS with unique kernel stack
- Per-CPU data access via `gs:` segment is CPU-local
- Cross-CPU data access returns local copy (not sibling's)

### Local APIC — test_lapic.cpp
- LAPIC mapped at correct MMIO base (0xFEE00000)
- LAPIC ID register matches APIC ID
- LAPIC version register returns valid value (0x0014 or similar)
- LAPIC timer one-shot fires at correct interval
- LAPIC timer periodic fires at correct interval
- LAPIC EOI clears in-service register
- X2APIC mode enabled if supported

### I/O APIC — test_ioapic.cpp
- I/O APIC found at correct MMIO base
- I/O APIC ID, version registers match expected values
- IRQ redirection entry routes IRQ to target CPU
- Mask/unmask IRQ via redirection entry mask bit
- Invalid IRQ vector rejected

### SMP Boot — test_smp_boot.cpp
- Secondary APs woken via INIT-SIPI-SIPI sequence
- All detected APs reach `ap_startup()` entry point
- Each AP has unique kernel stack and PML4
- Scheduler sees N+1 tasks after boot (BSP + N APs)

### Core State Isolation — test_core_isolation.cpp
- Each core has independent PML4
- Kernel stack per core is non-overlapping
- TSS IST stacks per core are independent
- Cross-core pointer corruption test (no leakage)

### IRQ Priority (TPR) — test_irq_priority.cpp
- APIC TPR blocks interrupts below threshold
- High-priority IRQ preempts low-priority handler
- TPR restored after handler completes
- NMI not blocked by TPR
