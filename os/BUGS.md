# Bug & Feature Tracking

## Critical Bugs

### ID: #009 — Second `fork()` in userspace shell creates child that never executes (CLOSED)
- **Description:** When the userspace shell forks a second child process (e.g., `garbing` then `xyzzy`), the second `fork()` call appears to succeed but the child task never executes. Fixed in commit `36fe819`.
- **Root Cause:** `sys_exit()` (`syscall_handlers_misc.cpp:47`) woke the parent by writing the exit code and setting `waiting_child_pid = 0`, but **left the child in the scheduler as a `TERMINATED` zombie** with `parent_id` still pointing to the shell. When the shell's `waitpid(-1, ...)` ran for the second command, `sys_waitpid()` scanned scheduler tasks for children with `parent_id == cur->id` and found the zombie first (still `TERMINATED`). Instead of waiting for the second child, `waitpid` immediately reaped the zombie and returned its PID. The second child was never waited on, and the shell printed the next prompt as if it had reaped the expected child.
- **Why the zombie survived:** `sys_waitpid` does NOT block-and-retry; when the child wasn't `TERMINATED` at call time, it sets `state = BLOCKED` and returns `-1`. `handle_interrupt_c` then calls `reschedule()`, which defers the context switch to the assembly epilogue in `isr_stubs.asm`. Because the context switch happens AFTER `handle_interrupt_c` returns (not within the syscall handler), the parent cannot retry the waitpid after being woken. It returns to userspace with `-1`, leaving the child as an unreaped zombie.
- **Why `reap_orphans` didn't clean it up:** `Scheduler::reap_orphans()` skips `TERMINATED` children whose parent is alive and `waiting_child_pid == 0` (the exact state after `sys_exit` wakes the parent). The zombie accumulated indefinitely.
- **Fix:** In `sys_exit()`, when waking a waiting parent, also call `p->remove_child(t)` and set `t->parent_id = 0` to orphan the child. This ensures:
  1. `reap_orphans()` cleans it up (`parent_id == 0` → `can_reap = true`)
  2. Future `waitpid()` calls never find it (no `parent_id` match)
  3. The second fork's child is found and properly waited on
- **Verification:** Two new kernel-mode tests (`waitpid_zombie_over_new_child`, `waitpid_two_children_sequential_reap`) validate the fix. All 1190 tests pass (3 pre-existing failures tracked as bug #010 unchanged).
- **Status:** Closed

### ID: #007 — `clone_kernel_pml4()` leaks identity-map; fork breaks without shared user entries
- **Description:** Boot code sets PML4[0] → PDPT_IDENTITY (0x2000) as an identity mapping (supervisor-only). `clone_kernel_pml4()` previously copied all 512 entries, leaking this kernel-owned page into user PML4s. User code crashing at addresses within the identity-map range got protection faults (err=0x5) because the pages were supervisor-only. Zeroing entries 0-255 fixed the identity-map leak but broke fork: the child's PML4 had no user-space mappings → `#PF` at entry point (err=0x4).
- **Root Cause:** `clone_kernel_pml4()` served two conflicting purposes: (a) creating a fresh PML4 for ELF load/exec (must not have identity-map), and (b) cloning page tables for fork (must share parent's user entries). A single function cannot do both.
- **Resolution — three changes:**
  1. `clone_kernel_pml4()` zeroes entries 0-255, copies 256-511 (fresh user PML4 for exec/load).
  2. `TaskControlBlock::clone()` (fork) allocates a raw PML4, copies user entries 0-255 from the PARENT's PML4 (sharing PDPT/PD/PT pages) and kernel entries 256-511 from the kernel PML4. Sets `page_table_shared_ = true`.
  3. `cleanup()` and `exec_into_current()` skip `free_user_pages()` when `page_table_shared_` is true (shared page tables must not be partially freed). The PML4 page itself is still freed.
- **Verification:** 6 dedicated tests (`pml4_clone_*`) validate all aspects — `clone_kernel_pml4()` clears user entries, kernel entries match, fork PML4 user entries match parent, child mapping doesn't corrupt parent, `free_user_pages` skips shared pages, and `dbg_dump_pml4` confirms no user-accessible entries in cloned PML4.
- **Status:** Closed (verified by `bug007_*` test suite)

### ID: #011 — Task lifecycle audit: elf::load missing init_task_common, blocked-sender abandonment, page_table_shared_ use-after-free (CLOSED)

- **Description:** Three bugs in the task lifecycle cleanup path were discovered during the v0.2.9 audit:
  1. `elf::load()` did not call `init_task_common()`, leaving `msg_queue`, `notify`, and `event_group` as `nullptr` for ELF-loaded tasks. Any subsequent IPC operation on these objects caused a null-pointer crash.
  2. When a task exited, any senders blocked on its message queue (`blocked_senders`) were stranded in `BLOCKED` state forever — no code woke them.
  3. A fork child (with `page_table_shared_ = true`) called `free_user_pages()` on cleanup, which recursively freed shared PDPT/PD/PT pages while the parent still referenced them → use-after-free.
- **Root Cause:**
  1. `elf::load()` created a `TaskControlBlock` via `new` but skipped `init_task_common()`, which is the only place `msg_queue`/`notify`/`event_group` are allocated for a fresh task.
  2. `TaskControlBlock::cleanup()` deleted `msg_queue` without iterating `blocked_senders_head` to wake any blocked tasks first.
  3. `cleanup()` unconditionally called `free_user_pages()` without checking `page_table_shared_`, even though `clone()` sets this flag to indicate the child shares page-table pages with the parent.
- **Fix (commit `50becea`):**
  1. Added `init_task_common(tcb)` in `elf::load()` so ELF-loaded tasks get fully initialized IPC objects.
  2. In `cleanup()`: before deleting `msg_queue`, iterate the `blocked_senders_head` list, set each blocked task's state to `READY`, and nullify the list. Only delete `msg_queue` when the list is empty.
  3. In `cleanup()`: guard `free_user_pages()` with `if (!page_table_shared_)` to skip recursive page-table freeing when the page table is shared with the parent. The PML4 page itself (which is per-task) is still freed.
- **Verification:** 12 kernel-mode tests in `test_task_lifecycle.cpp` and `test_task.cpp` validate all three fixes, including `elf_load_init_task_common_called`, `task_exit_wakes_blocked_senders`, `task_fork_child_cleanup_preserves_parent_pages`, and `task_exit_frees_page_tables_correctly`.
- **Status:** Closed

### ID: #010 — `Scheduler::reap_orphans()` doesn't reparent children to init or correctly reap deferred (CLOSED)
- **Description:** Three kernel-mode tests consistently failed:
  1. `scheduler_reap_orphans_can_reap_deferred`: After parent clears `waiting_child_pid = 0`, the terminated child was not reaped (`find_task` returned non-null).
  2. `task_reparent_preserves_resources`: After parent terminates and `reap_orphans()` runs, the orphaned child's `parent_id` did not equal the init task's ID.
  3. `scheduler_reap_respects_parent_wait`: Same as #1 — deferred reap never executed after wait was cleared.
- **Root Cause:** Two independent bugs:
  1. **Missing `waiting_child_pid == 0` reap condition** (`scheduler.cpp:250`): `reap_orphans()` only reaped a child when `p->waiting_child_pid` was non-zero and didn't match the child. When the parent cleared the wait to 0 (simulating collected status), no condition matched, so the child was never reaped. Fixed by adding `else if (p->waiting_child_pid == 0) can_reap = true`.
  2. **Out-of-bounds `current_index_` in `remove_task`** (`scheduler.cpp:52`): `remove_task()` used `current_index_ > task_count_` to detect an out-of-bounds index, but `current_index_ == task_count_` is also out of bounds. When a test called `set_current(task)` followed by `remove_task(task)`, `current_index_` could equal `task_count_` (one past the last valid index). This caused subsequent tests to have `current_index_` pointing to an unexpected task (the parent). `reap_orphans()` then skipped the parent due to the `t == current` guard. Fixed by changing `>` to `>=`.
- **Fix:**
  1. `scheduler.cpp:256-258`: Added `else if (p->waiting_child_pid == 0)` condition so clearing the wait allows immediate child reaping.
  2. `scheduler.cpp:52`: Changed `current_index_ > task_count_` to `current_index_ >= task_count_` so the exact out-of-bounds case is corrected.
- **Verification:** All 1194 tests pass (previously 3 failures, now 0).
- **Status:** Closed

### ID: #012 — Release build test_fork: wrong waitpid result + kernel panic (CLOSED)
- **Description:** `make run-release-test` would either crash with a kernel panic or produce wrong waitpid results. The test_fork userspace test expected `waitpid(child_pid, &status, 0)` to return the child's PID and set status to the child's exit code (42). Instead, it returned -1 with status=0x0, followed by a kernel panic from page-fault in `TaskControlBlock::create()` called by `reap_orphans()` after the boot identity map was removed.
- **Root Cause:** Three independent bugs:
  1. **Missing HHDM_OFFSET in `create()`** (`task.cpp:84-90`): `TaskControlBlock::create()` initialized the kernel stack with raw `stack_phys` instead of `arch::HHDM_OFFSET + stack_phys`. All other callers (`create_user()`, `clone()`) used the HHDM-offset address. After the boot identity map was removed during VMM setup, the raw physical address became unmapped, causing a page fault when `reap_orphans()` called `create()` to reparent a task.
  2. **Status write in wrong address space** (`syscall_handlers_misc.cpp`): `sys_exit()` wrote the child's exit code to `parent->context.rsp[0]` (the parent's saved RAX on the kernel stack) correctly, but also tried to write to the parent's user-space `status` pointer **while running in the child's context** — using the child's page table. The cross-address-space write landed in the wrong physical page.
  3. **Blocking syscall return value** (`syscall_handlers_misc.cpp`): When `waitpid()` blocks (returns -1 from the syscall handler because the child isn't ready yet), the kernel stores -1 in `context.rsp[0]` (RAX). When the child later exits, `sys_exit()` must override this to return the child's PID. The override was done for the status pointer but not for the RAX return-value slot.
- **Fix (commit `4c229a9`):**
  1. `task.cpp:84-90`: Changed `stack_phys` to `arch::HHDM_OFFSET + stack_phys` in `TaskControlBlock::create()`.
  2. `syscall_handlers_misc.cpp`: In `sys_exit()`, before writing to the parent's user-space `status` pointer, switch to the parent's page table via `arch::write_cr3(parent->page_table_)`. Restore the child's CR3 afterward.
  3. `syscall_handlers_misc.cpp`: After writing the child's PID to the parent's `context.rsp[0]` (RAX slot on kernel stack), ensure the parent receives the correct return value.
- **Verification:** `make run-release-test` now passes with "ALL TESTS PASSED". 163/163 kernel tests pass in both debug and release builds. test_fork userspace test prints `waitpid returned 8, status=0x2a`.
- **Status:** Closed

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
