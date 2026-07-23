# Test Cases — v0.3.6 (Phase 4: Cross-Architecture Hard Real-Time HAL)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### x86_64 — APIC + TSC-Deadline — test_hal_x86_64.cpp
- `arch/x86_64/hal/apic.hpp/.cpp`: Local APIC, I/O APIC, TSC-deadline timer
- Invariant TSC verified via `CPUID.80000007H:EDX[8]`
- `arch/x86_64/hal/tsc.hpp`: `rdtsc`/`rdtscp` wrappers, TSC calibration
- `arch/x86_64/hal/msr.hpp`: MSR read/write for `IA32_TSC_DEADLINE`, `IA32_TSC_AUX`
- TSC-deadline timer fires at exact programmed cycle count
- APIC timer + TSC-deadline switchable at runtime via config

### ARM64 — GICv3 + Generic Timer — test_hal_aarch64.cpp
- `arch/aarch64/hal/gic.hpp/.cpp`: GICD/GICR/GICC init, priority, affinity routing
- `arch/aarch64/hal/timer.hpp/.cpp`: ARM Generic Timer (`CNTVCT_EL0`, `CNTPCT_EL0`)
- `arch/aarch64/hal/context.hpp`: AArch64 register save/restore, FP/SIMD
- `arch/aarch64/boot.cpp`: EL2→EL1 transition, MMU init, GIC init
- Generic Timer fires at correct interval
- Virtual count (`CNTVCT_EL0`) monotonic across all cores

### RISC-V64 — PLIC + CLINT/S-Mode Timer — test_hal_riscv64.cpp
- `arch/riscv64/hal/plic.hpp/.cpp`: PLIC init, priority, threshold, claim/complete
- `arch/riscv64/hal/timer.hpp/.cpp`: CLINT (`mtime`/`mtimecmp`) or SBI timer
- `arch/riscv64/hal/context.hpp`: RISC-V register save/restore, FP
- `arch/riscv64/boot.cpp`: M-mode→S-mode, PMP, MMU (Sv39/Sv48), PLIC init
- Timer interrupt fires at correct interval in S-mode

### Common Hard-RT HAL Interface — test_hal_interface.cpp
- `arch/hal/hard_rt.hpp`: pure virtual interface — Timer, InterruptController, Context, IPI
- Compile-time selection via `CONFIG_ARCH_*` — no runtime polymorphism overhead
- WCET annotations on all HAL functions (comments with measured max cycles)
- Each architecture implementation passes the same interface conformance test
