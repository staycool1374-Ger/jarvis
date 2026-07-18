# ipc_blocking Flakiness — Root-Cause Analysis (2026-07-18)

Single-core x86_64 kernel. `ipc_blocking` test class has **two distinct failure
modes**. One is FIXED; the other is characterized but NOT yet fixed.

## FIXED: H1 — send_sync reply loss (caused `LEAK: Tasks -3` FAIL)

- **Symptom:** test 2 `ipc_send_sync_was_blocked_restores_state` intermittently
  FAILs with `LEAK: Tasks -3`.
- **Root cause:** `IPC::send_sync` (`src/kernel/ipc/ipc.cpp:259-265`) bails with
  `return false` when the peer is `TERMINATED`, *before* re-checking its own
  reply queue. The receiver's normal lifecycle terminates *after* delivering its
  reply (`IPC::send` → `WAKE` into sender's queue). So a delivered reply was
  discarded and `send_sync` reported failure.
- **Proof (deterministic trace):**
  ```
  [SEND] to=7 from=6 ty=99 q=1
  [WAKE] dest=7 st=2 inrq=0 q=1        # reply queued into sender(7)
  [SYNC-FAIL] dest-gone-reply cur=7 dest=6 q=1   # bail despite queued reply
  S: ... 2/4: FAIL [LEAK: Tasks -3]
  ```
- **Fix (F1, committed-in-tree, uncommitted):** when peer gone, only `return
  false` if the queue is genuinely empty; otherwise `break` and let the existing
  `pop(reply)` consume the delivered reply. Honors the IPC contract (reply in
  own queue ⇒ success).
- **Verification:** 0 FAIL across 20+ runs after the fix (was ~1/8 before).

## OPEN: H2 — deferred-switch / userspace-task crash in test 3 & 4 (HANG)

Two HANG subtypes, both rooted in test 3/4's **userspace task** design +
the deferred context-switch machinery. ~45% of runs hang.

### Subtype A — SIGSEGV (task 6 user lambda)
- Task 6 (user task in `ipc_userspace_block_uses_sti_hlt_cli`) SIGSEGVs:
  `CR2=0xFFFF80000022C32F` (= kernel address of the test lambda `FUN`),
  `err=0x15` (user write to supervisor page), `CR3=0x1909000` (kernel PML4).
- `nm` maps `0xFFFF80000022C32F` →
  `_ZZ41test_ipc_userspace_block_uses_sti_hlt_clivENUlvE_4_FUNEv` — the kernel
  lambda used as the user task's entry point.
- `scheduler.cpp:[SW] next=6 cr3=26251264` shows the switch is set up with the
  CORRECT user PML4 (26251264), but the ISR applies **kernel PML4** (0x1909000)
  → mismatched RSP/CR3 pair. This is the split-phase deferred switch:
  `switch_to_task` writes `scheduler_load_rsp_from` and `scheduler_load_cr3_from`
  as two separate stores; a timer ISR firing between them (or the
  `next_is_runner` self-promotion path at `scheduler.cpp:1329` returning without
  re-publishing the pair) lets the ISR apply a stale CR3 with a new RSP.
- **Attempted fixes (all REVERTED — did not reduce hang rate):** (1) defuse
  stale trigger at start of `switch_to_task`; (2) spin-wait for pending switch to
  be consumed (made it WORSE — deadlock under IRQs-off `yield_as`);
  (3) consume pending switch in the `next_is_runner` early-return.

### Subtype B — live-lock (task 6 never completes)
- `scheduler.cpp:[SW] next=6 cr3=26251264 rsp=18446603336247414608` repeats
  **634,962 times** with an IDENTICAL constant RSP — task 6 is selected as
  `next` every tick, switched to, runs one step, yields/reschedules back to the
  same point, never advances. No SIGSEGV.
- Task 6 does `IPC::send(self_id, msg)` then `IPC::recv(out)`. The self-sent
  message should be in its own queue → `recv` pops immediately → completes. The
  live-lock means `recv` does NOT see the message and blocks (hlt-loops) forever,
  while `next_task()` keeps returning the blocked-but-still-"ready" task 6
  (`in_ready_queue_` desync, INV-2).

### Underlying design issue
`create_user` (`task.cpp:379,392`) sets the user task's RIP/RDI to the
**kernel virtual address** of the C++ lambda. A true user task cannot execute
supervisor pages (Subtype A) and the way it "blocks" depends on `sys_receive`
internals that the lambda does not invoke (it calls `IPC::recv` directly, which
does NOT block). Whether the run SIGSEGVs (A) or live-locks (B) depends on the
active page-table permissions at the moment of dispatch — non-deterministic due
to the deferred-switch CR3 race. The **test infrastructure itself is suspect**:
a userspace task should reach `IPC` via a syscall wrapper, not call kernel
`IPC::send`/`IPC::recv` directly.

## Diagnostic harness (SAVED, reusable — `CONFIG_DEBUG_IPC_SCHED`)
- `src/kernel/debug/ipc_sched_trace.hpp` — printf-free serial trace macros,
  gated by `CONFIG_DEBUG_IPC_SCHED` (enabled). Toggle the `#define` to reuse.
- Traces emitted: `[SEND]`, `[WAKE]`, `[SYNC]`, `[SYNC-FAIL]` (with
  `dest-gone-empty` vs `dest-gone-reply` discrimination), `[SW]` (next/cr3/rsp),
  `[WEDGE]` (orphan scan: READY/RUNNING task with `in_ready_queue_=1` but not
  physically linked).
- `TaskQueue::contains()` added for the orphan physical-link check.
- These are NON-INVASIVE (trace-only); safe to leave enabled.

## Next steps (for the hang / H2)
1. Decide whether test 3/4's user task should call IPC via a **syscall** rather
   than the kernel `IPC::` class directly. If the test is "correct by design",
   the kernel must make userspace entry execute properly (map kernel .text as
   user-accessible in cloned PML4, or copy the thunk to a user mapping).
2. Fix the deferred-switch RSP/CR3 atomicity: publish the pair under a single
   seqlock/generation so the ISR never applies a half-written pair. The ISR
   apply path is `src/kernel/arch/x86_64/isr_stubs.asm:106-171`.
3. Fix `in_ready_queue_` desync: ensure a BLOCKED task is unlinked from the runq
   (the `next_task()` lazy rebuild heals it, but the live-lock shows it is not
   being reached for task 6).
