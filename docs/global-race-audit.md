# Global / Volatile Variable Race Audit

**Date:** 2026-08-01
**Scope:** All global and `volatile` variables used for synchronization across
ISR context, task context, and nested IRQs in `src/kernel`.
**Status:** Initial audit — "MAYBE" rows scheduled for per-variable review in
ROADMAP v0.3.7.

## Classification

- **SAFE** — variable is accessed only via `__atomic_*` operations, or only from
  a single execution context (boot, init-once, ISR-only, idle-only), or is fully
  lock/IRQ-guarded.
- **MAYBE** — plain (non-atomic) read/write across two contexts (ISR vs task, or
  nested on_tick), or mixed atomic/non-atomic access to the same variable. Needs
  a closer per-variable investigation (ROADMAP v0.3.7 VAR-01..17).

---

## ✅ SAFE (definitely no race concern)

| Variable | Declaration / kind | Access pattern | Why safe |
|---|---|---|---|
| `scheduler_save_rsp_to` | `extern "C" uint64_t *` | `__atomic_load/store` ACQ/REL + asm | Fully atomic |
| `scheduler_load_rsp_from` | `extern "C" uint64_t` | `__atomic_load/store` ACQ/REL | Fully atomic |
| `scheduler_load_cr3_from` | `extern "C" uint64_t` | `__atomic_load/store` ACQ/REL | Fully atomic |
| `scheduler_next_task_id` | `extern "C" uint64_t` | `__atomic_load/store` ACQ/REL | Fully atomic |
| `scheduler_need_resched` | `extern "C" bool` | `__atomic_load/store` ACQ/REL | Fully atomic |
| `zombie_count_` | `static constinit uint64_t` | `__atomic_add/sub_fetch` RELAXED | Fully atomic |
| `deadline_detection_integrity` | `extern "C" uint64_t` | `__atomic_fetch_add` RELEASE | Fully atomic |
| `scheduler_corruption_count` | `extern "C" uint64_t` | `__atomic_fetch_add` RELEASE | Fully atomic |
| `fpu_owner` | `extern "C" TaskControlBlock *` | `__atomic_load/store` ACQ/REL | Fully atomic |
| `s_next_generation` | `static uint32_t` (task.cpp) | `__atomic_fetch_add` | Fully atomic |
| `multiboot_magic` / `multiboot_info_ptr` | `extern constinit uint64_t` | boot-once write, read-only after | Single writer at boot |
| `g_boot_epoch` / `g_boot_ns` | `extern uint64_t` | written once at kernel.cpp:838 | Single writer at boot |
| `g_pmm_init_done` | `static bool` (pmm.cpp) | init-once flag | Single writer at init |
| `g_vector_init` | `static bool` (pci.cpp) | init-once flag | Single writer at init |
| `root_initialized` (fat32/initrd) | `static bool` | init-once flag | Single writer at init |
| `mount_count` (vfs.cpp) | `static size_t` | vfs task context only | Single context |
| `num_daemons_` / `suppress_death_msg_` | `static` (daemon_mgr.cpp) | daemon task context only | Single context |
| `pmm_lock_` / `mempool_lock_` | `static sync::SpinLock` | guard all PMM/MemPool ops | Lock-guarded |
| `PMM::free_pages_`, `bitmap_`, ... | `static constinit uint64_t` | under `pmm_lock_` | Lock-guarded |
| `VMM::kernel_pml4_` | `static constinit uint64_t` | boot-once, read-only after | Single writer |
| `irq_entry_tsc` | `extern uint64_t` (asm) | written+read in same ISR (asm r8) | Same ISR context |
| `crc_*` (integrity.cpp) | `static` | idle task only | Single context |
| `s_kstack_pt_pages` | `static uint64_t[8]` | boot-once init | Single writer |
| `g_vfs_touched` | `extern bool` | test single-context | Single context |
| `g_recent_tasks_idx` | `extern size_t` | debug write-log, single context | Single context |
| `s_tcb_write_idx` / `s_tcb_write_seq` | `static` (tcb_write_log.cpp) | debug write-log, single context | Single context |

---

## ⚠️ MAYBE — needs closer per-variable review (ROADMAP v0.3.7)

| VAR | Variable | Declaration | Concern | Candidate fix |
|---|---|---|---|---|
| VAR-01 | `current_task_ptr_` | `static constinit TaskControlBlock *` | Plain pointer: written task-context (`set_current`, `terminate`) AND ISR (switch epilogue via `scheduler_on_context_switch`), read in ISR+task. No atomic. | Read under `IrqGuard` or `__atomic_load`; reconcile with scheduler-spec INV-1 (RSP-authoritative vs cache) |
| VAR-02 | `isr_nesting_depth` | `extern "C" uint64_t` | asm `inc`/`dec` (non-atomic RMW) + `__atomic_store` reset (scheduler.cpp:2094) + plain read (dump.cpp) — mixed access kinds | Make all C++ accesses atomic; asm inc/dec OK (IRQ-off in ISR) |
| VAR-03 | `s_scan_requested_` | `volatile bool` | line 2318 plain `= false` alongside `__atomic_store`/`__atomic_exchange` — mixed atomic/plain | Unify to atomics (drop volatile or use atomics consistently) |
| VAR-04 | `s_test_active_` | `bool` | task write (test.cpp:402/484), ISR read (on_tick:1104, RMS:1832) | IrqGuard or atomic |
| VAR-05 | `preempt_enabled_` | `constinit bool` | boot/restore write (421,2074), ISR read (on_tick:878) | Atomic or IrqGuard |
| VAR-06 | `sporadic_task_count_` | `constinit uint64_t` | restore write (2075), ISR read (on_tick:1209 loop bound) | Atomic |
| VAR-07 | `suppress_terminated_log_` | `constinit bool` | task write, read in `reap_orphans` (ISR+task) | Atomic or gate |
| VAR-08 | `s_deferred_kill_count` + `s_deferred_kill_tasks[]` | `static uint64_t` + `static TaskControlBlock*[16]` | plain RMW: `defer_kill` (deadline-miss ISR + tests) vs `process_deferred_kills` (on_tick, now gated) | Atomic count / fully gated |
| VAR-09 | `Timer::ticks_` | `constinit volatile uint64_t` | RMW `ticks_ = ticks_+1` in ISR, volatile read in task; 64-bit RMW/read not guaranteed atomic on aarch64/riscv | `__atomic` load/store, or align+document |
| VAR-10 | `Keyboard::shift_/ctrl_/alt_/caps_` | `constinit bool` | ISR write / task read, plain bools | Atomic byte or gate |
| VAR-11 | `MessageQueue::head/tail/count` | `volatile size_t` | `is_empty()/is_full()` read `count` without `lock_` | Guard unlocked readers or atomics |
| VAR-12 | `BufferPool::next_cookie_/pool_count_` | `constinit uint32_t/size_t` | plain RMW (`++`/`--`), no lock, task-context callers only | IrqGuard/lock or document single-core invariant |
| VAR-13 | `s_wedge_emitted_`, `s_last_switch_tick_` | `static uint64_t` (CONFIG_DEBUG) | on_tick + `scheduler_on_context_switch` (ISR) | Atomic or gate under single context |
| VAR-14 | `s_lk0_count`, `s_last_holder` | `static` (CONFIG_DEBUG, on_tick locals) | ISR or task-context on_tick overlap | Gate on single on_tick context |
| VAR-15 | `g_test_deadline_monitor_pid` | `extern uint64_t` | task write, ISR read (deadline_miss_handler) | Atomic |
| VAR-16 | `scheduler_dummy_save_rsp` | `extern uint64_t` | task write (reschedule) / ISR read | Atomic or gate |
| VAR-17 | `hhdm_modified_` | `static bool` | task write (map_page) / task read (test_isolate) — same context, no lock | Atomic or document single-core |

---

## Notes

- All `SAFE` atomic variables use explicit `__atomic_*` builtins — they are
  correct under the C++ memory model and immune to compiler reordering.
- `volatile` alone (VAR-09, VAR-11) does **not** provide atomicity; it only
  prevents compiler caching. On x86_64 aligned 64-bit access is atomic in
  practice, but aarch64/riscv need verification.
- The `on_tick` tail (deferred-kill flush, sporadic block, reap/flush watchdog)
  was already gated under `if (lock_acquired) { arch::IrqGuard ... }` in the
  same session — that addresses the cross-context tail sections; the variables
  above are the residual standalone candidates.
