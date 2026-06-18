# Test Cases — v0.12.14 (Phase 3: Observability & Portability)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Kernel Log Ringbuffer — test_klog.cpp
- `SYS_KLOG` reads back kernel log entries
- Ringbuffer wraps without data corruption
- Multiple tasks reading KLOG concurrently
- KLOG with invalid buffer pointer returns EFAULT
- `dmesg` utility prints kernel log

### Hardware Abstraction Layer (HAL) — test_hal.cpp
- `ArchPageTable`: map/unmap, clone, switch
- `ArchContext`: save/restore, switch_to
- `ArchInterruptController`: mask/unmask, EOI
- `ArchTimer`: one-shot, periodic, remaining
- `ArchIO`: in/out byte/word/long
- Each HAL method delegates to correct arch/x86_64 implementation

### Directory Migration — test_arch_structure.cpp
- All x86_64-specific code lives under `arch/x86_64/`
- Generic `arch/` contains only interface headers
- Build system selects correct arch directory

### Multi-Arch Build — test_buildsystem.cpp
- `ARCH=x86_64` selects x86_64 toolchain and sources
- Invalid ARCH value produces clear error message
- Default ARCH is x86_64

### Secure Exec — test_secure_exec.cpp
- `SYS_EXEC` validates argv/envp via CheckedPtr
- Null argv pointer returns EFAULT
- argv/envp in kernel space returns EFAULT
- argv/envp crosses into unmapped region returns EFAULT
- Valid argv/envp executes successfully
- All 492+ regression tests pass after HAL refactor
