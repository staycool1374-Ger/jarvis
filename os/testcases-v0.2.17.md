# Test Cases — v0.2.17 (Kernel Synchronization & Real-Time Guarantees)

### SpinLock — test_spinlock.cpp (new — 8 tests)
- `spinlock_basic_lock_unlock` — lock, write shared flag, unlock; verify flag written
- `spinlock_contention` — 2 tasks contend for same SpinLock; both complete without corruption
- `spinlock_preemption_yield` — high-pri task preempts low-pri lock-holder; verify lock yield mechanism
- `spinlock_cxx_contract` — SpinLockGuard is [[nodiscard]], non-copyable, non-movable (compile check)
- `spinlock_no_irqguard_inside` — static_assert or runtime check: no cli() called during SpinLock critical section
- `spinlock_recursive_deadlock_detect` — DEBUG mode: same task locking twice triggers ASSERT
- `spinlock_pause_count` — spin count during contention is bounded (< 10k iterations on unlocked)
- `spinlock_stress_1000_ops` — 1000 lock/unlock cycles across 3 tasks, verify final invariant

### SPSC Ring Buffer — test_spsc_ring.cpp (new — 6 tests)
- `spsc_basic_push_pop` — push 1, pop 1; value matches
- `spsc_fifo_ordering` — push 1,2,3; pop 1,2,3 in order
- `spsc_full_reports_false` — fill ring, try_push returns false
- `spsc_empty_reports_false` — empty ring, try_pop returns false
- `spsc_power2_wrap` — verify wrap-around at power-of-2 boundary (size=8, push 12 items)
- `spsc_producer_consumer_tasks` — ISR-mocked producer task + consumer task, 1000 items

### Context-Switch Atomic — test_atomic_context_switch.cpp (new — 4 tests)
- `atomic_globals_acquire_release` — scheduler stores with release, ISR reads with acquire; verify ordering
- `atomics_idempotent_null_handling` — scheduler_load_rsp_from=0 → no RSP load; scheduler_load_cr3_from=0 → no CR3 load
- `atomics_fpu_owner_relaxed` — fpu_owner atomic relaxed access in #NM handler
- `atomics_assembly_bridge` — C bridge functions return correct values readable by isr_stubs.asm

### Preemption Under Syscall — test_preemption_under_syscall.cpp (new — 3 tests)
- `preempt_highpri_during_tmpfs_write` — high-pri periodic task (1 kHz) + low-pri shell writing to tmpfs; high-pri meets deadline (< 1 tick jitter)
- `preempt_highpri_during_brk` — high-pri periodic task + low-pri calling sys_brk in loop; high-pri meets deadline
- `preempt_lowpri_not_starved` — low-pri task eventually completes under high-pri load

### IPC Lock-Free — test_ipc_lock_free.cpp (new — 3 tests)
- `ipc_recv_no_cli` — verify sys_receive() does not call cli() (probe via interrupt flag check before/after)
- `ipc_send_sync_no_cli` — verify send_sync() does not call cli()
- `ipc_lock_free_throughput` — 1000 IPC roundtrips, measure throughput vs baseline (must not regress)

### IrqGuard Audit — test_irqguard_audit.cpp (new — 1 test)
- `irqguard_remaining_sites_validated` — enumerate all remaining #include <arch/irq_guard.hpp> sites; only boot, panic, test isolation must remain

### Benchmark — bench_syscall_latency.cpp (new — 2 benchmarks)
- `bench_syscall_latency_before_after` — RDTSC-measured empty syscall latency (expected: same as before, ≈ no regression)
- `bench_irq_latency_worst_case` — PIT one-shot + timestamp at ISR entry; measure worst-case IRQ latency under tmpfs write load (expected: < 10 µs, before: up to 500 µs with IrqGuard)