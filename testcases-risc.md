# RISC-V (riscv64) — Missing Architecture-Specific Test Cases

## Prompt
Implement the following test cases under `src/kernel/arch/riscv64/test_riscv64.cpp` (new file) and register them via `register_riscv64_tests()` — add a new registration function and wire it in the kernel's test init. Each test must use `JARVIS_TEST()` / `JARVIS_REGISTER_TEST()` macros, follow existing patterns in `src/kernel/test/test_hal.cpp`, and be guarded with `#if defined(CONFIG_ARCH_RISCV64)`. All tests run in kernel mode.

## Test Cases

### 1. Sv39 Page Table — 3-Level Walk Correctness
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_sv39_3level_walk`
- **Description**: Walks the active SATP page table at a known kernel VA (e.g. `0xFFFFFFC080207000`, the `.text` base). Decodes L0 index (bits 38:30), verifies the L0 entry has V=1 and R=W=X=0 (table pointer). Descends to L1, checks V=1 and (R|W|X) for leaf detection or R=W=X=0 for table. Finally checks L2 leaf has V=1, (R|W|X) ≥ 1, and A=1.
- **Dependencies**: `arch::read_cr3()` (returns PA from SATP), HHDM offset for VA->PA
- **Assert**: Each level's entry is valid, physical address bits are correct, A/D bits set on leaf entries.

### 2. Sv39 Page Table — Map/Unmap via ArchPageTable
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_sv39_map_unmap`
- **Description**: Allocates a physical page via `PMM::alloc_page()`, maps it at an unused kernel VA (e.g. `0xFFFFFFC08043F000`) using `arch::ArchPageTable::map_page()` with PRESENT|READ|WRITE|EXEC|ACCESSED|DIRTY flags. Reads back via `get_physical()`, verifies correct PA. Calls `unmap_page()`, verifies `get_physical()` returns 0.
- **Dependencies**: `PMM`, `ArchPageTable::map_page/unmap_page/get_physical`
- **Assert**: `get_physical` returns correct PA after map, returns 0 after unmap.

### 3. Sv39 Page Table — 2MB Block Split via VMM
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_sv39_block_split`
- **Description**: Maps a 2MB-aligned VA (e.g. `0xFFFFFFC080600000`) via `VMM::map_page()` — this creates a 2MB block in the boot page tables. Then maps a 4KB page at VA+0x1000 (inside the block) via `VMM::map_page_user()`. Verifies the L1/L2 page table structure now has a table pointer (V=1, R=W=X=0) instead of a leaf block entry (V=1, R|W|X=1), and the L2 leaf for the 4KB page returns the correct PA.
- **Dependencies**: `VMM::map_page()`, `VMM::virt_to_phys()`, manual table walk
- **Assert**: After split, intermediate entry is a table pointer; 4KB leaf entry exists with correct PA.

### 4. Context Switch — Register Save/Restore
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_context_save_restore`
- **Description**: Creates two `ArchContext` instances, calls `ArchContextManager::save()` on each with known SP values, then calls `switch_to()` to swap. Verifies saved ra, sp, s0–s11 registers match expected values and that switch_to correctly updates the current stack pointer.
- **Dependencies**: `arch::ArchContext`, `arch::ArchContextManager`
- **Assert**: Saved ra/sp/s0–s11 preserved, current_rsp updated to target SP.

### 5. Context Switch — SRET Trap Frame Construction
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_context_sret_frame`
- **Description**: Calls `ArchContextManager::init_stack()` (or `create_user()`) to construct a trap frame for SRET to U-mode. Verifies `sepc` matches entry function, `sstatus` has SPP=0 (U-mode), SPIE=1 (interrupts enabled on return), and the stack pointer is within the provided stack region.
- **Dependencies**: `arch::ArchContextManager::create_user()` or `init_stack()`
- **Assert**: sepc == entry, sstatus.SPP == 0, sstatus.SPIE == 1.

### 6. PLIC — Interrupt Controller Init
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_plic_init`
- **Description**: Calls `arch::ArchInterruptController::init()`, then reads PLIC priority and enable registers via MMIO. Asserts that at least one interrupt source is enabled (e.g. UART IRQ 10 on QEMU virt). Verifies the PLIC threshold register is set to 0 (all priorities accepted).
- **Dependencies**: `arch::ArchInterruptController::init()`, PLIC MMIO at `0xC000000` (QEMU virt)
- **Assert**: Threshold == 0, at least 1 interrupt source enabled in `enable` registers.

### 7. PLIC — Mask/Unmask Interrupt
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_plic_mask_unmask`
- **Description**: Masks UART interrupt (IRQ 10) via `arch::ArchInterruptController::mask(10)`, reads the PLIC enable register to confirm the bit is cleared. Unmasks, confirms the bit is set again. Ensures the kernel does not lose UART output during the mask window.
- **Dependencies**: `arch::ArchInterruptController::mask/unmask()`
- **Assert**: After mask, enable bit for IRQ 10 == 0. After unmask, enable bit == 1.

### 8. PLIC — Claim/Complete (EOI) Cycle
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_plic_claim_complete`
- **Description**: Calls `arch::ArchInterruptController::eoi(10)` (UART IRQ). Reads the PLIC claim register to verify the current claim is 0 (no pending interrupt). Acknowledges the EOI path does not fault. Repeats for IRQ 1 (virtio) to verify multi-IRQ EOI stability.
- **Dependencies**: `arch::ArchInterruptController::eoi()`
- **Assert**: No fault; claim register reads 0 after EOI (no pending interrupt).

### 9. SBI Timer — Set Timer via Ecall
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_sbi_timer_set_stime`
- **Description**: Reads the current `mtime` value via the SBI TIME extension (`sbi_get_time()`). Sets a timer 100,000 ticks in the future via SBI `set_timer()` ecall. Verifies the ecall returns SBI_SUCCESS (no error). Reads `mtime` again and confirms it advanced.
- **Dependencies**: SBI ecall interface (`ebreak`/`ecall` with SBI convention), `arch::Timer::read_mtime()` or SBI time function
- **Assert**: SBI call returns success, mtime advances monotonically.

### 10. SBI Timer — Tick Counter Monotonic
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_timer_ticks_monotonic`
- **Description**: Reads `arch::Timer::ticks()` in a tight loop of 10 iterations. Asserts each successive read is ≥ the previous read (monotonic non-decreasing). Requires no interrupt interaction — pure counter read from the kernel's tick counter.
- **Dependencies**: `arch::Timer::ticks()`
- **Assert**: ticks[n] ≥ ticks[n-1] for all n.

### 11. SBI Timer — ns() Conversion Accuracy
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_timer_ns_conversion`
- **Description**: Calls `arch::Timer::ns()` to get the nanosecond timestamp, sleeps for a known delay (spinning loop of ~1M iterations), then calls `ns()` again. Asserts the delta is positive and within reasonable bounds (> 0, < 10 seconds).
- **Dependencies**: `arch::Timer::ns()`, delay loop
- **Assert**: ns delta > 0 and < 10_000_000_000 ns (10 seconds).

### 12. RISC-V FPU — F/D Extension Detection
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_fpu_extension_detection`
- **Description**: Reads `misa` CSR via inline `csrr` to extract the F (single-precision float, bit 5) and D (double-precision float, bit 3) extension flags. Asserts both are set on QEMU virt (which emulates `rv64imafdc`). Verifies `arch::cpuid()` reports the extensions correctly.
- **Dependencies**: `csrr %0, misa`, `arch::cpuid()`
- **Assert**: MISA[5] (F) == 1, MISA[3] (D) == 1.

### 13. SBI Console — Putchar via Ecall
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_sbi_console_putchar`
- **Description**: Calls `arch::Serial::putc('R')` to print a single character, then reads the UART line status register (LSR at `0x10000005`) to confirm TEMT (Transmitter Empty, bit 6) is set. Repeats for all printable ASCII (0x20–0x7E) to bulk-test the SBI putchar path.
- **Dependencies**: `arch::Serial::putc()`, NS16550A UART LSR register
- **Assert**: LSR TEMT bit set after each putc; no character lost.

### 14. PCI ECAM — Config Space Read
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_pci_ecam_read`
- **Description**: Reads Vendor ID/Device ID of bus 0, device 0, function 0 via `arch::pci_read_config_dword()`. Asserts Vendor ID ≠ 0xFFFF (device present). Verifies the same value matches the aarch64 ECAM result (QEMU virt uses the same PCI topology across archs).
- **Dependencies**: `arch::pci_read_config_dword()` (ECAM on riscv64)
- **Assert**: Vendor ID valid (≠ 0xFFFF, ≠ 0x0000).

### 15. RTC — mtime-based Wall Clock
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_rtc_mtime_read`
- **Description**: Calls `arch::RTC::read()` to obtain the current wall clock. Asserts year ≥ 2025 and ≤ 2035 (sanity range). Calls twice with a short delay and verifies time advances or stays equal (monotonic).
- **Dependencies**: `arch::RTC::read()` (mtime/mtimecmp based)
- **Assert**: Year in valid range, time monotonic.

### 16. SATP — CSR Read/Write
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_satp_csr`
- **Description**: Reads SATP via `arch::read_cr3()`. Asserts the returned PA is non-zero and page-aligned. Asserts the MODE field (bits 63:60) == 8 (Sv39). Writes the same value back via `arch::write_cr3()`, then re-reads and confirms it matches. Calls `sfence.vma` and verifies no fault.
- **Dependencies**: `arch::read_cr3()`, `arch::write_cr3()`, `arch::ArchPageTable::tlb_flush_all()`
- **Assert**: SATP mode == 8, cr3 PA is non-zero and page-aligned.

### 17. Boot — Machine Vendor ID
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_boot_mvendorid`
- **Description**: Reads `mvendorid` CSR via inline `csrr`. Asserts it matches QEMU's value (0x0 for QEMU, or the generic platform ID). Reads `marchid` and `mimpid` CSRs and verifies they are non-zero.
- **Dependencies**: `csrr %0, mvendorid`, `csrr %0, marchid`, `csrr %0, mimpid`
- **Assert**: marchid ≥ 0x1 (at minimum RISC-V I spec), mimpid ≥ 0.

### 18. S-mode Interrupt Delegation
- **File**: `src/kernel/arch/riscv64/test_riscv64.cpp`
- **Name**: `riscv64_medeleg_selected`
- **Description**: Reads `medeleg` CSR (Machine Exception Delegation) via `csrr`. Asserts that at least ecall-from-U-mode (exception 8) is delegated to S-mode (medeleg bit 8 == 1). Reads `mideleg` (Machine Interrupt Delegation) and asserts timer interrupt (bit 5) is delegated.
- **Dependencies**: `csrr %0, medeleg`, `csrr %0, mideleg`
- **Assert**: medeleg[8] == 1 (ecall from U-mode), mideleg[5] == 1 (timer interrupt).