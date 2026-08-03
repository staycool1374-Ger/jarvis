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

## ⚠️ MAYBE — per-variable review (v0.3.7 PfA remediation)

Remediation status (2026-08-03, `docs/v0.3.7-pfa-concurrency-design.md` steps 1–10):
- **DONE** — variable remediated in the v0.3.7 PfA work.
- **BY-DESIGN** — already single-owner under an explicit lock/context; documented, no code change needed.
- **PHASE-8** — full per-CPU GS/TPIDR-relative asm access deferred to SMP groundwork.

| VAR | Variable | Status | Candidate fix applied |
|---|---|---|---|
| VAR-01 | `current_task_ptr_` | DONE | Backed by `CpuContext::current`; published via `set_current_ptr()`; RSP-ownership scan stays authoritative (INV-1) |
| VAR-02 | `isr_nesting_depth` | DONE (PHASE-8 asm) | All C++ access `__atomic_*`; asm inc/dec IRQ-off (GS-relative per-CPU deferred to Phase 8) |
| VAR-03 | `s_scan_requested_` | DONE | All accesses `__atomic_*`; plain `= false` writes removed; `volatile` dropped |
| VAR-04 | `s_test_active_` | DONE | Moved to injected `TestContext::test_active`; no scheduler global |
| VAR-05 | `preempt_enabled_` | DONE | `SchedulerConfig::preempt_enabled` via `Scheduler::init(cfg)`; runtime toggles via existing setters (snapshot/restore state) |
| VAR-06 | `sporadic_task_count_` | DONE | `SchedulerConfig::sporadic_task_count`; runtime inc/dec already atomic |
| VAR-07 | `suppress_terminated_log_` | DONE | `SchedulerConfig::suppress_terminated_log` via `init(cfg)` |
| VAR-08 | `s_deferred_kill_count` + `s_deferred_kill_tasks[]` | BY-DESIGN | All access already under `scheduler_lock_` (on_tick/scan_deadlines guarded tail); documented |
| VAR-09 | `Timer::ticks_` | DONE | `__atomic_fetch_add`/`load`; accessor `Timer::ticks()` unchanged |
| VAR-10 | `Keyboard::shift_/ctrl_/alt_/caps_` | DONE | Packed into one byte-atomic `mods_` with `__atomic_fetch_or/and` |
| VAR-11 | `MessageQueue::head/tail/count` | DONE | Unlocked `is_empty()/is_full()` use `__atomic_load_n(RELAXED)`; RMW remain under `lock_` |
| VAR-12 | `BufferPool::next_cookie_/pool_count_` | DONE | `__atomic_fetch_add` cookie; `__atomic_add/sub_fetch` page count |
| VAR-13 | `s_wedge_emitted_`, `s_last_switch_tick_` | DONE | Folded into `CpuContext::wedge_emitted` / `last_switch_tick` (per-CPU debug) |
| VAR-14 | `s_lk0_count`, `s_last_holder` | DONE | Folded into `CpuContext::lk0_count` / `last_holder` |
| VAR-15 | `g_test_deadline_monitor_pid` | DONE | Moved to `TestContext::deadline_monitor_pid` |
| VAR-16 | `scheduler_dummy_save_rsp` | DONE | Removed (write-only dead global); TestContext `dummy_save_rsp` reserved |
| VAR-17 | `hhdm_modified_` | PHASE-8 | Task-context only, single-core safe; re-audit under SMP (no change in v0.3.7) |

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
