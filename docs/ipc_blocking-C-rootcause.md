# ipc_blocking — Root-Cause Analysis (C): deferred-switch single-slot buffer corruption

## Symptom (post-B)
After fix (B) (publish RSP/CR3 pair last + defuse stale trigger), `ipc_blocking`
still fails ~25% of runs. Failure mode shifted from a HARD FREEZE to:
```
[ERROR] [TEST:FAIL] src/kernel/test/test_ipc_blocking.cpp:94: ok
```
Receiver task 6 never receives the sender's (task 7) message. Root = the
receiver runs but reads the WRONG message queue because `current_task_ptr_`
lags the physically-running task.

## Deferred-switch design (single-core)
- `switch_to_task()` does NOT switch immediately. It only *publishes* a pending
  switch into 4 single-slot atomics:
    - `scheduler_save_rsp_to`   (save target = old TCB's RSP slot)
    - `scheduler_load_rsp_from` (new task's RSP)
    - `scheduler_load_cr3_from` (new task's PML4)
    - `scheduler_next_task_id`  (id of the task to become `current`)
  (`src/kernel/task/scheduler.cpp:1496-1533`)
- The actual switch is applied by the timer ISR
  (`src/kernel/arch/x86_64/isr_stubs.asm:106-171`): save old RSP, load new RSP,
  call `scheduler_on_context_switch()` which sets `current_task_ptr_` from
  `scheduler_next_task_id` (`scheduler.cpp:2143-2150`).

## ROOT CAUSE
**Only ONE pending switch is buffered.** If the scheduler selects a switch
A→B and then B→C before the next timer ISR fires, the second `switch_to_task`
overwrites all four atomics. The A→B switch is silently lost:
- B's RSP is never saved into B's TCB (the A→B save_target is overwritten by
  the B→C save_target).
- When the ISR applies A→C, `current_task_ptr_` becomes C, but B's TCB still
  holds B's pre-B RSP while B may be READY in the runq.
- `IPC::recv`/`IPC::send` identify the running task via `current_task_ptr_`
  (`src/kernel/ipc/ipc.cpp:166,212,227,245`). A lagged `current_task_ptr_`
  makes a running task read/write the WRONG TCB's `msg_queue` → messages are
  routed to the wrong task → receiver (task 6 here) sees an empty queue and
  asserts false (line 94).

This explains BOTH historical failure modes:
- HARD FREEZE: CR3/RSP mismatch (mismatched user task on kernel PML4) when the
  overwritten pair is half-applied — mitigated by (B) but not eliminated.
- RECEIVER-NEVER-RECEIVES (current post-B mode): `current_task_ptr_` lag.

## Why localized fixes failed
Attempts (defuse-v1 spin-wait, consume-on-promote, test-3 convert, defuse-
corrected) all tune the single-slot buffer. The buffer itself is the defect:
any two scheduler decisions between two timer ticks corrupt state. This is not
tunable away.

## Required fix (scheduler redesign — beyond bandaid)
One of:
1. **Serialise switches**: do not buffer; if a switch is already pending when
   `switch_to_task` is called, either (a) drop the new selection and let the
   next ISR pick again from the runq, or (b) queue pending switches as a small
   ring. Option (a) is simplest and matches "next_task() will re-select".
2. **Apply the switch synchronously** when IRQL permits (no ISR pending), and
   only defer when inside an ISR context.
3. **Make `current_task_ptr_` authoritative from the RSP owner** on every
   `current_task()` read (already done in `switch_to_task` heal path at
   `scheduler.cpp:1366-1389`; extend to the ISR apply path so a lagged pointer
   is corrected the instant RSP is swapped).

Until the single-slot buffer is fixed, `ipc_blocking` (a stress test of rapid
back-to-back IPC-driven switches) will remain flaky.

## Files
- src/kernel/task/scheduler.cpp: switch_to_task (1496-1539), on_context_switch (2143-2150)
- src/kernel/arch/x86_64/isr_stubs.asm:106-171
- src/kernel/ipc/ipc.cpp:166,212,227,245 (current_task() routing)
- src/kernel/task/scheduler.hpp:406 (current_task_ptr_)
