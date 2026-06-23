# Test Cases — v0.2.20 (System Calls & Storage)

## Completed — 680/680 tests passing

### SYS_YIELD — test_syscall.cpp
- [x] `syscall_dispatch_yield` — YIELD syscall returns 0. Already implemented in v0.2.19.

### SYS_REBOOT / SYS_HALT — test_syscall.cpp
- [x] `syscall_dispatch_reboot` — REBOOT syscall number/datapath verified (actual reset skipped in test).
- [x] `syscall_dispatch_halt` — HALT syscall number/datapath verified (actual halt skipped in test).
- **Files modified:** `syscall.hpp`, `syscall_handlers_misc.cpp`, `libc/syscall.h`, `test_syscall.cpp`, `test_microkernel_transition.cpp`

### DMA Completion Interrupt — test_dma.cpp + dma.hpp/dma.cpp
- [x] `dma_completion_interrupt` — DmaEngine + BmDmaChannel API verified: start_transfer, is_busy, handle_irq, callback, abort.
- **New abstractions:**
  - `DmaChannel` — abstract interface for DMA register access (supports kernel I/O and future user IPC backends)
  - `BmDmaChannel` — kernel-mode BMDMA via PCI port I/O (implements DmaChannel)
  - `DmaEngine` — high-level manager using DmaChannel + completion callback dispatch

### Double-Buffered DMA (Ping-Pong) — test_dma.cpp + dma.hpp/dma.cpp
- [x] `dma_double_buffered_transfer` — PingPongDma with two alternating buffers, automatic chaining, data integrity verification.
- **New abstraction:** `PingPongDma` — manages two DMA buffers with automatic chaining, completion callback notification.

### AHCI/SATA Driver with NCQ — test_block_device.cpp + ahci.cpp/ahci_protocol.hpp
- [x] `ahci_protocol_struct_sizes` — CmdFIS (20B), CmdHeader (32B), PrdHbaEntry (16B), ReceivedFis (256B) verified.
- [x] `ahci_protocol_constants` — Register offsets, GHC bits, ATA commands, FIS types verified.
- [x] `ahci_hba_probe` — Hardware probe gracefully handles absence, verifies BlockDevice interface when present.
- **New files:**
  - `ahci_protocol.hpp` — standalone shared header (AHCI register layouts, FIS types, PRD, command structures) usable by kernel and userspace drivers
  - `ahci.hpp` — AhciDriver class (extends BlockDevice, PCI probe, port init, command submission, NCQ support)
  - `ahci.cpp` — Full implementation: HBA reset, capability detection, port command engine, non-NCQ DMA (READ/WRITE DMA EXT), NCQ (READ/WRITE FPDMA QUEUED), IDENTIFY, polling completion

### Architecture & Boot Integration
- Boot probe tries AHCI first, falls back to legacy ATA PIO.
- DmaChannel abstraction enables clean DMA engine interface for both kernel BMDMA and future userspace AHCI drivers.