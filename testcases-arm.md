# ARM64 (aarch64) — Missing Architecture-Specific Test Cases

## Prompt
Implement the following test cases under `src/kernel/arch/aarch64/test_aarch64.cpp` (new file) and register them via `register_aarch64_tests()` in the existing arch test registration framework. Each test must use `JARVIS_TEST()` / `JARVIS_REGISTER_TEST()` macros, follow existing patterns in `src/kernel/test/test_hal.cpp`, and be guarded with `#if defined(CONFIG_ARCH_AARCH64)`. All tests run in kernel mode.

## Test Cases

### 1. Page Table — 4-Level Walk Correctness
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_page_table_4level_walk`
- **Description**: Verifies the aarch64 4-level page table walk (L0→L1→L2→L3) at VA 0xFFFF800040000000 (kernel text). Walks each level using `ArchPageTable` internals: reads TTBR1_EL1, decodes L0 index (bits 47:39), verifies table entry has `DESC_VALID | DESC_TABLE`, then descends to L1/L2/L3, confirming each level's descriptor type and physical address alignment.
- **Dependencies**: `arch::read_ttbr1_el1()`, `arch::ArchPageTable::current()`
- **Assert**: Each level's entry is valid, table pointers are page-aligned (4KB), L3 leaf entries have `DESC_AF` set.

### 2. Page Table — Map Leaf Page via ArchPageTable
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_page_table_map_leaf`
- **Description**: Allocates a free page via `PMM::alloc_page()`, maps it at an unused kernel VA (e.g. `0xFFFF80004000F000`) via `arch::ArchPageTable::map_page()` with RW flags, reads back via `get_physical()`, unmaps, and confirms `get_physical()` returns 0.
- **Dependencies**: `PMM`, `ArchPageTable::map_page/unmap_page/get_physical`
- **Assert**: `get_physical` returns correct PA after map, returns 0 after unmap.

### 3. Page Table — 2MB Block Split
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_page_table_block_split`
- **Description**: Maps a 2MB block at an L2-aligned VA, then maps a 4KB page inside that block (triggering the VMM's huge-page-split path). Verifies the L2 entry changes from block descriptor to table descriptor, and the new L3 page table contains the original block pages + the new mapping.
- **Dependencies**: `VMM::map_page()` on kernel page table, `ArchPageTable::get_physical()`
- **Assert**: After split, L2 entry has `DESC_TABLE` set, L3 leaf for target VA matches the newly mapped physical page.

### 4. Context Switch — Register Save/Restore
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_context_save_restore`
- **Description**: Creates two `ArchContext` instances, calls `ArchContextManager::save()` on each with known SP values, then calls `switch_to()` to swap between them. Verifies after switch that the saved SP and callee-saved registers (x19–x28) are preserved in the context structures.
- **Dependencies**: `arch::ArchContext`, `arch::ArchContextManager`
- **Assert**: Saved SP matches input, switch_to changes current_rsp to the target context's SP.

### 5. Context Switch — Init Stack Frame
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_context_init_stack`
- **Description**: Calls `ArchContextManager::init_stack()` to construct a fresh trap frame for EL0 return. Verifies `SPSR_EL1` has correct EL0/EL1 mode bits, `ELR_EL1` matches entry function, and SP_EL0 points within the provided stack region.
- **Dependencies**: `arch::ArchContextManager::init_stack()`
- **Assert**: SPSR_EL1 indicates EL0 execution, ELR_EL1 matches entry point, stack pointer is in range.

### 6. GICv3 — Interrupt Controller Init
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_gic_init`
- **Description**: Calls `arch::ArchInterruptController::init()`, then verifies distributor and CPU interface are enabled by reading GICD_CTLR and GICC_CTLR via MMIO. Asserts that the distributor has at least 32 interrupt lines (GICD_TYLR.ITLinesNumber > 0).
- **Dependencies**: `arch::ArchInterruptController::init()`, GIC MMIO
- **Assert**: GICD_CTLR.Enable=1, GICC_CTLR.Enable=1, ITLinesNumber ≥ 1.

### 7. GICv3 — Mask/Unmask SPI
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_gic_mask_unmask`
- **Description**: Masks an unused SPI (e.g. IRQ 64) via `arch::ArchInterruptController::mask(64)`, reads back the GICD_ICENABLER register to confirm the bit is set, then unmask and verify via GICD_ISENABLER. Uses the GIC distributor MMIO registers at the known QEMU virt base.
- **Dependencies**: `arch::ArchInterruptController::mask/unmask()`
- **Assert**: After mask, the interrupt is disabled in GICD_ICENABLER. After unmask, re-enabled.

### 8. GICv3 — EOI Cycle
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_gic_eoi`
- **Description**: Calls `arch::ArchInterruptController::eoi(32)` (a known QEMU virt UART interrupt). Verifies no crash and that subsequent mask/unmask operations work correctly after EOI.
- **Dependencies**: `arch::ArchInterruptController::eoi()`
- **Assert**: No exception, subsequent mask/unmask succeed.

### 9. ARM Generic Timer — Counter Monotonic
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_timer_counter_monotonic`
- **Description**: Reads `arch::Timer::ticks()` (via CNTPCT_EL0 or the internal tick counter) in a tight loop of 10 iterations. Asserts each successive read is ≥ the previous read (monotonic non-decreasing). Requires no interrupt interaction — pure counter read.
- **Dependencies**: `arch::Timer::ticks()`
- **Assert**: ticks[n] ≥ ticks[n-1] for all n.

### 10. ARM Generic Timer — Frequency Query
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_timer_frequency`
- **Description**: Reads `arch::Timer::frequency()` or `arch::Timer::ns()` conversion factor. Asserts the reported frequency is within the valid range for QEMU virt (CNTFRQ_EL0, typically 62.5 MHz = 62_500_000 Hz, ±10%).
- **Dependencies**: `arch::Timer::frequency()` or `CNTFRQ_EL0` read
- **Assert**: Frequency is between 10 MHz and 200 MHz, and timer::ns() for 1 second of ticks is ≈ 1_000_000_000 ns.

### 11. MMU — Cache and TLB Maintenance
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_mmu_cache_tlb`
- **Description**: Calls `arch::ArchPageTable::tlb_flush_all()` and `arch::ArchPageTable::tlb_flush(virt)` for a known VA. Verifies no fault occurs and that subsequent page table operations succeed. Calls `dsb_sy()` and `isb()` barrier instructions directly via inline asm and verifies they don't fault (sanity check for barrier correctness).
- **Dependencies**: `arch::ArchPageTable`, inline asm for dsb/isb
- **Assert**: No faults; TLB flush operations complete without error.

### 12. FPU/SIMD — NEON Feature Detection
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_fpu_neon_detection`
- **Description**: Reads `ID_AA64PFR0_EL1` via inline `mrs` to extract FP and SIMD fields. Asserts both fields are ≥ 1 (FP implemented, Advanced SIMD implemented) as required by ARMv8-A. Verifies `arch::cpuid()` returns the correct value for FP feature bits.
- **Dependencies**: `arch::cpuid()`, or direct `ID_AA64PFR0_EL1` read
- **Assert**: FP field (bits 15:12) ≥ 1, SIMD field (bits 19:16) ≥ 1.

### 13. PL011 UART — Loopback Putc
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_uart_putc`
- **Description**: Calls `arch::Serial::putc('A')` and verifies no fault. Reads the UART FR (Flag Register) to confirm TXFE (Transmit FIFO Empty) is set after the write. Verifies putc of all printable ASCII characters (0x20–0x7E) completes without error.
- **Dependencies**: `arch::Serial::putc()`, PL011 FR register at `0x9000000` (QEMU virt)
- **Assert**: No exception; TX FIFO empty after write.

### 14. PCI ECAM — Config Space Read
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_pci_ecam_read`
- **Description**: Reads the Vendor ID and Device ID of the first PCI bus (bus 0, device 0, function 0) via `arch::pci_read_config_dword(0, 0, 0, 0)`. Asserts Vendor ID ≠ 0xFFFF (at least one device present on QEMU virt). Compares with x86 CF8/CFC results known from test_pci.cpp.
- **Dependencies**: `arch::pci_read_config_dword()` (ECAM path)
- **Assert**: Vendor ID is valid (≠ 0xFFFF, ≠ 0x0000), Device ID is non-zero for device 0.

### 15. RTC — Wall Clock Read
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_rtc_read`
- **Description**: Calls `arch::RTC::read()` to obtain current wall clock time. Asserts the returned year is ≥ 2025 and ≤ 2035 (sanity range). Calls twice with a short delay and verifies time moves forward or stays equal (no regression).
- **Dependencies**: `arch::RTC::read()` (ARM Generic Timer based)
- **Assert**: Year in valid range, time advances monotonically.

### 16. Boot — Device Tree Pointer
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_boot_dtb_pointer`
- **Description**: Reads the saved device-tree blob pointer (passed from `boot.S` via `higherhalf_entry()`). Asserts it is non-zero and falls within a valid RAM range (0x40000000–0x50000000 for QEMU virt). Verifies the first 4 bytes of the DTB match the magic number `0xD00DFEED` (big-endian).
- **Dependencies**: Global DTB pointer variable set during boot
- **Assert**: DTB pointer is valid (non-null, in RAM range, magic matches).

### 17. Boot — Exception Vector Table
- **File**: `src/kernel/arch/aarch64/test_aarch64.cpp`
- **Name**: `aarch64_exception_vector_installed`
- **Description**: Reads `VBAR_EL1` via inline `mrs`. Asserts it is non-zero and matches the expected linker symbol `_vectors` (or is close to `_start`). Verifies that the first 128 entries (VBAR_EL1 + 0x000–0x7C0) are populated with valid branch instructions.
- **Dependencies**: Inline `mrs VBAR_EL1, %0`
- **Assert**: VBAR_EL1 ≠ 0, aligned to 2KB.