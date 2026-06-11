# Bug & Feature Tracking

## Critical Bugs

### ID: #009 — Second `fork()` in userspace shell creates child that never executes
- **Description:** When the userspace shell forks a second child process (e.g., `garbing` then `xyzzy`), the second `fork()` call appears to succeed (no error returned) but the child task never executes a single instruction. A `write(1, "CHILD\n", 6)` placed immediately after `fork()` in `run_command()` prints for the first child but not the second. The parent does not print a `"DEBUG: fork failed"` message either, which would appear if `fork()` returned `< 0`. The parent unblocks from `waitpid()` quickly (no timeout), suggesting either no child exists to wait for or the child exits before any code runs.
- **Symptoms (release build, QEMU UEFI, `-serial mon:stdio`):**
  - First command (`garbing`): child outputs `CHILD\nsh: garbing: not found`, parent reaps, prints next prompt — OK.
  - Second command (`xyzzy`): child never outputs `CHILD`, parent prints next `sh$ ` prompt immediately. Expect 10-second timeout hits before any "not found" output.
  - `make run-release-test` (regex pattern with `\r+\n`) times out on command #2.
  - `expect` glob patterns (`"*sh: xyzzy: not found*"`) also time out.
  - Removing the `write(1, "CHILD\n", 6)` debug line does NOT fix the issue.
- **Kernel state during failure (observed via test output):**
  - Fork table has capacity (max_tasks=40, ~7-8 in use).
  - Kernel heap (1 MiB bump allocator in `src/lib/new.cpp`) has ample space — `operator delete` is a no-op stub but the bump allocator never runs out.
  - `operator delete` being a no-op means per-child `TaskControlBlock`, `MessageQueue`, `Notify`, `EventGroup` objects from the first fork are leaked, but the 1 MiB heap should accommodate hundreds of such allocations before exhaustion.
  - PMM page allocations (kernel stack, page tables, user stack) are freed by `cleanup()` and should be available for the second fork.
- **Suspected root cause (not yet confirmed):**
  - The first child exits via `_exit(127)`. `sys_exit` in `syscall_handlers_misc.cpp:47` marks the task `TERMINATED`, reparents children, and wakes a blocked parent. The parent's `waitpid(-1, ...)` in `syscall_handlers_process.cpp:23` reaps the child via `remove_child()`, `cleanup()`, `remove_task()`, `delete`. If `remove_task()` leaves the scheduler in an inconsistent state (e.g., `current_index_` points past `task_count_`), the second child may never be scheduled.
  - Alternatively, `Scheduler::add_task()` could race with `test_fork` (PID 0x2A, priority 1, running concurrently with shell priority 2) during the second `fork()`, corrupting the task array.
- **Test commands used:** `garbing`, `xyzzy`, `plugh`, `help`, `test`, `nonexistent`, `exit`
- **Reproducibility:** 100% on macOS (Apple Silicon, QEMU 9.x via Homebrew)
- **Status:** Open — needs investigation of `Scheduler::remove_task()` side effects and potential race with `test_fork`.

### ID: #007 — `clone_kernel_pml4()` leaks identity-map; fork breaks without shared user entries
- **Description:** Boot code sets PML4[0] → PDPT_IDENTITY (0x2000) as an identity mapping (supervisor-only). `clone_kernel_pml4()` previously copied all 512 entries, leaking this kernel-owned page into user PML4s. User code crashing at addresses within the identity-map range got protection faults (err=0x5) because the pages were supervisor-only. Zeroing entries 0-255 fixed the identity-map leak but broke fork: the child's PML4 had no user-space mappings → `#PF` at entry point (err=0x4).
- **Root Cause:** `clone_kernel_pml4()` served two conflicting purposes: (a) creating a fresh PML4 for ELF load/exec (must not have identity-map), and (b) cloning page tables for fork (must share parent's user entries). A single function cannot do both.
- **Resolution — three changes:**
  1. `clone_kernel_pml4()` zeroes entries 0-255, copies 256-511 (fresh user PML4 for exec/load).
  2. `TaskControlBlock::clone()` (fork) allocates a raw PML4, copies user entries 0-255 from the PARENT's PML4 (sharing PDPT/PD/PT pages) and kernel entries 256-511 from the kernel PML4. Sets `page_table_shared_ = true`.
  3. `cleanup()` and `exec_into_current()` skip `free_user_pages()` when `page_table_shared_` is true (shared page tables must not be partially freed). The PML4 page itself is still freed.
- **Verification:** 6 dedicated tests validate all aspects — `clone_kernel_pml4()` clears user entries, kernel entries match, fork PML4 user entries match parent, child mapping doesn't corrupt parent, `free_user_pages` skips shared pages, and `dbg_dump_pml4` confirms no user-accessible entries in cloned PML4.
- **Status:** Closed (verified by `bug007_*` test suite)

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
