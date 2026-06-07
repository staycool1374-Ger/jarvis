# Bug & Feature Tracking

## Critical Bugs

### ID: #007 — `clone_kernel_pml4()` leaks boot identity-map into user page tables
- **Description:** The boot code sets up PML4[0] → PDPT_IDENTITY (0x2000) as an identity-mapping page table. `VMM::clone_kernel_pml4()` copies *all* 512 PML4 entries, including this identity-map entry. When a user task terminates, `VMM::free_user_pages()` walks entries 0-255 and asserts `PMM::is_user_page(0x2000)` — but that PDPT page was allocated by the bootloader as a kernel page, not a user page.
- **Root Cause:** `clone_kernel_pml4()` blindly memcpy's the kernel PML4 instead of only copying kernel-space entries (256-511) and zeroing user-space entries (0-255).
- **Severity:** Critical (System Crash/Deadlock)
- **Domain:** Security Boundary
- **Status:** Resolved — fixed at `vmm.cpp:clone_kernel_pml4()` to only copy entries 256-511 and zero 0-255.

## Recommendations (Future Features)

### ID: #001 — AHCI/SATA Driver
- **Description:** The FAT32 filesystem (v0.2.9) targets ATA PIO on QEMU's emulated IDE controller. Real hardware uses AHCI/SATA with NCQ. An AHCI driver is needed for bare-metal storage access.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #002 — Networking Stack (TCP/IP)
- **Description:** The OS currently has no networking capability. A full networking stack (ARP, IP, ICMP, UDP, TCP) with an Ethernet driver is required for distributed real-time communication, remote logging, and networked control systems.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #003 — USB Stack
- **Description:** The OS currently uses PS/2 for keyboard input. Modern hardware relies on USB (UHCI/EHCI/xHCI). A USB stack is needed to support USB keyboards, mice, and storage on real machines.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #004 — SYS_yield
- **Description:** No way for a task to cooperatively yield the CPU. Preemption already works, but a `SYS_YIELD` syscall would allow cooperative scheduling for CPU-bound tasks that want to share the core voluntarily.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #005 — SYS_reboot / SYS_halt
- **Description:** No clean way to reboot or power off the system from userspace. A `SYS_REBOOT`/`SYS_HALT` syscall pair is needed for shutdown scripts and init-level power management.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #006 — Lock-Free Ring Buffer (ISR→Task Handoff)
- **Description:** ISRs currently block or drop data when the target task's queue is full. A wait-free single-producer/single-consumer ring buffer for ISR-to-task handoff would eliminate priority inversion and data loss in interrupt context.
- **Severity:** Recommendation (New Feature)
- **Domain:** Safety-Critical (ASIL/SIL impact)
- **Status:** Open
