# Bug & Feature Tracking

## Critical Bugs

### ID: #009 — Second `fork()` in userspace shell creates child that never executes (CLOSED)
- **Description:** When the userspace shell forks a second child process (e.g., `garbing` then `xyzzy`), the second `fork()` call appears to succeed but the child task never executes. Fixed in commit `?????`.
- **Root Cause:** `sys_exit()` (`syscall_handlers_misc.cpp:47`) woke the parent by writing the exit code and setting `waiting_child_pid = 0`, but **left the child in the scheduler as a `TERMINATED` zombie** with `parent_id` still pointing to the shell. When the shell's `waitpid(-1, ...)` ran for the second command, `sys_waitpid()` scanned scheduler tasks for children with `parent_id == cur->id` and found the zombie first (still `TERMINATED`). Instead of waiting for the second child, `waitpid` immediately reaped the zombie and returned its PID. The second child was never waited on, and the shell printed the next prompt as if it had reaped the expected child.
- **Why the zombie survived:** `sys_waitpid` does NOT block-and-retry; when the child wasn't `TERMINATED` at call time, it sets `state = BLOCKED` and returns `-1`. `handle_interrupt_c` then calls `reschedule()`, which defers the context switch to the assembly epilogue in `isr_stubs.asm`. Because the context switch happens AFTER `handle_interrupt_c` returns (not within the syscall handler), the parent cannot retry the waitpid after being woken. It returns to userspace with `-1`, leaving the child as an unreaped zombie.
- **Why `reap_orphans` didn't clean it up:** `Scheduler::reap_orphans()` skips `TERMINATED` children whose parent is alive and `waiting_child_pid == 0` (the exact state after `sys_exit` wakes the parent). The zombie accumulated indefinitely.
- **Fix:** In `sys_exit()`, when waking a waiting parent, also call `p->remove_child(t)` and set `t->parent_id = 0` to orphan the child. This ensures:
  1. `reap_orphans()` cleans it up (`parent_id == 0` → `can_reap = true`)
  2. Future `waitpid()` calls never find it (no `parent_id` match)
  3. The second fork's child is found and properly waited on
- **Verification:** Two new kernel-mode tests (`bug009_zombie_reaps_first`, `bug009_two_children_sequential_reap`) validate the fix. All 1190 tests pass (3 pre-existing failures tracked as bug #010 unchanged).
- **Status:** Closed

### ID: #007 — `clone_kernel_pml4()` leaks identity-map; fork breaks without shared user entries
- **Description:** Boot code sets PML4[0] → PDPT_IDENTITY (0x2000) as an identity mapping (supervisor-only). `clone_kernel_pml4()` previously copied all 512 entries, leaking this kernel-owned page into user PML4s. User code crashing at addresses within the identity-map range got protection faults (err=0x5) because the pages were supervisor-only. Zeroing entries 0-255 fixed the identity-map leak but broke fork: the child's PML4 had no user-space mappings → `#PF` at entry point (err=0x4).
- **Root Cause:** `clone_kernel_pml4()` served two conflicting purposes: (a) creating a fresh PML4 for ELF load/exec (must not have identity-map), and (b) cloning page tables for fork (must share parent's user entries). A single function cannot do both.
- **Resolution — three changes:**
  1. `clone_kernel_pml4()` zeroes entries 0-255, copies 256-511 (fresh user PML4 for exec/load).
  2. `TaskControlBlock::clone()` (fork) allocates a raw PML4, copies user entries 0-255 from the PARENT's PML4 (sharing PDPT/PD/PT pages) and kernel entries 256-511 from the kernel PML4. Sets `page_table_shared_ = true`.
  3. `cleanup()` and `exec_into_current()` skip `free_user_pages()` when `page_table_shared_` is true (shared page tables must not be partially freed). The PML4 page itself is still freed.
- **Verification:** 6 dedicated tests validate all aspects — `clone_kernel_pml4()` clears user entries, kernel entries match, fork PML4 user entries match parent, child mapping doesn't corrupt parent, `free_user_pages` skips shared pages, and `dbg_dump_pml4` confirms no user-accessible entries in cloned PML4.
- **Status:** Closed (verified by `bug007_*` test suite)

### ID: #010 — `Scheduler::reap_orphans()` doesn't reparent children to init or correctly reap deferred
- **Description:** Three tests consistently fail, all related to task lifecycle and reaping:
  1. `scheduler_reap_orphans_can_reap_deferred` (`test_scheduler.cpp:137`): After parent clears wait status and calls `reap_orphans()`, the terminated child is not removed from the scheduler (`find_task(child_id)` still returns non-null).
  2. `task_reparent_preserves_resources` (`test_task_lifecycle.cpp:146`): After parent terminates and `reap_orphans()` runs, the orphaned child's `parent_id` does not equal the init task's ID — reparenting to init fails.
  3. `scheduler_reap_respects_parent_wait` (`test_task_lifecycle.cpp:217`): Same as #1 — deferred reap never executes after wait status is cleared.
- **Suspected root cause:** `Scheduler::reap_orphans()` in `src/kernel/task/scheduler.cpp` checks `can_reap()` which requires the parent to be `TERMINATED`. For the deferred reap case (parent alive but waiting cleared), the parent is still `READY`, so the child is never reaped. Reparenting logic may only fire for certain state combinations or skip non-kernel tasks.
- **Test commands:** `make test-qemu` (fails consistently)
- **Reproducibility:** 100%
- **Status:** Open

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
