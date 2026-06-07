# Bug & Feature Tracking

## Critical Bugs

### ID: #008 — GRUB video modules (`insmod all_video`/`efi_gop`) trigger VMM crash during framebuffer HHDM mapping
- **Description:** When GRUB loads video modules (`insmod all_video` or `insmod efi_gop`), the UEFI GOP framebuffer is placed at physical address 0x80000000 (2 GiB). The kernel's framebuffer init attempts to map this region both as identity (0x80000000) and via HHDM (0xFFFF800080000000). The HHDM mapping requires page tables for the 2 GiB virtual address, but the boot HHDM only covers the first 128 MiB (via PML4[256] → PDPT_HIGHER[0] with 64 2 MiB huge pages). Page table pages allocated above 128 MiB had no HHDM mapping, causing a GPF in `VMM::map_page` when trying to write the PTE via the unmapped HHDM virtual address.
- **Root Cause:** `VMM::get_table()` returned `HHDM_OFFSET + new_page` for newly allocated page table pages, assuming the HHDM mapping existed. For pages allocated above 128 MiB (the framebuffer and subsequent page table pages), the HHDM mapping was absent. The temporary page mapping (`map_temp`/`unmap_temp`) used PML4[511] but accessed via the wrong virtual address (identity vs PML4[511] range), so page table pages were never zeroed.
- **Resolution — three changes:**
  1. Added `PMM::alloc_page_table()` that allocates page table pages from a reserved 16 MiB pool within the first 128 MiB of physical memory (guaranteed covered by boot identity map and HHDM huge pages).
  2. Simplified `VMM::get_table()` to use the identity mapping (virtual = physical) for zeroing newly allocated page table pages, since they now always reside in the boot-mapped low memory.
  3. Removed the broken `map_temp`/`unmap_temp` PML4[511] temporary mapping mechanism entirely.
- **Status:** Resolved (v0.2.6)

### ID: #007 — `clone_kernel_pml4()` leaks identity-map; fork breaks without shared user entries
- **Description:** Boot code sets PML4[0] → PDPT_IDENTITY (0x2000) as an identity mapping (supervisor-only). `clone_kernel_pml4()` previously copied all 512 entries, leaking this kernel-owned page into user PML4s. User code crashing at addresses within the identity-map range got protection faults (err=0x5) because the pages were supervisor-only. Zeroing entries 0-255 fixed the identity-map leak but broke fork: the child's PML4 had no user-space mappings → `#PF` at entry point (err=0x4).
- **Root Cause:** `clone_kernel_pml4()` served two conflicting purposes: (a) creating a fresh PML4 for ELF load/exec (must not have identity-map), and (b) cloning page tables for fork (must share parent's user entries). A single function cannot do both.
- **Resolution — three changes:**
  1. `clone_kernel_pml4()` zeroes entries 0-255, copies 256-511 (fresh user PML4 for exec/load).
  2. `TaskControlBlock::clone()` (fork) allocates a raw PML4, copies user entries 0-255 from the PARENT's PML4 (sharing PDPT/PD/PT pages) and kernel entries 256-511 from the kernel PML4. Sets `page_table_shared_ = true`.
  3. `cleanup()` and `exec_into_current()` skip `free_user_pages()` when `page_table_shared_` is true (shared page tables must not be partially freed). The PML4 page itself is still freed.
- **Status:** Resolved (v0.2.5)

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
