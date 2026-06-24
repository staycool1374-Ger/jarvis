# Test Cases — v0.2.21 (Kernel Configuration & Portability)

## Quality Gate
Before v0.2.21 can be released all the following tests must be either:
- Implemented and passing, OR
- Explicitly deferred to a later version with a documented reason

## Pre-existing Verification (already passing)
- `buildsystem_debug_flags` — CONFIG_DEBUG defined in debug builds
- `buildsystem_config_arch_defined` — CONFIG_ARCH_X86_64 defined for x86-64 builds
- `make check-config` — 14 validation checks pass with default config
- `make test-all-debug` — 680/680 tests pass

## 1. check-config Validation Script — test_check_config.py

### 1.1 Positive validation
- [ ] `check_config_defaults` — `python3 tools/check-config.py` exits 0 with default config
- [ ] `check_config_all_architectures` — Validate with `ARCH=x86_64`, `ARCH=aarch64`, `ARCH=riscv64` all pass

### 1.2 Negative validation (bad values caught)
- [ ] `check_config_rejects_bad_max_tasks` — CONFIG_MAX_TASKS < 2 or > 4096 causes non-zero exit
- [ ] `check_config_rejects_bad_tick_hz` — CONFIG_TICK_HZ < 100 causes non-zero exit
- [ ] `check_config_rejects_non_power_of_2_page` — CONFIG_PAGE_SIZE = 8191 (not power of 2) rejected
- [ ] `check_config_rejects_stack_lt_min` — CONFIG_STACK_SIZE < CONFIG_MIN_STACK_SIZE rejected
- [ ] `check_config_rejects_bad_priority_ceiling` — CONFIG_PRIORITY_CEILING = 0 or 256 rejected
- [ ] `check_config_rejects_bad_max_priority` — CONFIG_MAX_PRIORITY = 100 (not power of 2) rejected
- [ ] `check_config_rejects_unaligned_hhdm` — CONFIG_HHDM_OFFSET not page-aligned rejected
- [ ] `check_config_rejects_bad_task_name_len` — CONFIG_TASK_NAME_LEN < 4 or > 256 rejected

### 1.3 Missing validation checks to add
- [ ] `check_config_mempool_list_lengths` — CONFIG_MEMPOOL_BLOCK_SIZES / COUNTS list length != CONFIG_MEMPOOL_NUM_POOLS rejected
- [ ] `check_config_timeslicing_depends_preemption` — CONFIG_TIME_SLICING=1 with CONFIG_PREEMPTION=0 rejected
- [ ] `check_config_priority_ceiling_vs_max` — CONFIG_PRIORITY_CEILING > CONFIG_MAX_PRIORITY rejected
- [ ] `check_config_arch_mutual_exclusion` — Multiple CONFIG_ARCH_* defined simultaneously rejected

## 2. Compile-Time Configuration Sanity — test_config_sanity.cpp

### 2.1 Core config values present
- [ ] `config_version_defined` — `#ifdef CONFIG_VERSION` passes
- [ ] `config_max_tasks_sane` — `CONFIG_MAX_TASKS >= 2 && CONFIG_MAX_TASKS <= 4096` at compile-time
- [ ] `config_page_size_power_of_2` — `(CONFIG_PAGE_SIZE & (CONFIG_PAGE_SIZE - 1)) == 0`
- [ ] `config_arch_mutually_exclusive` — Exactly one of CONFIG_ARCH_X86_64, CONFIG_ARCH_AARCH64, CONFIG_ARCH_RISCV64 is defined
- [ ] `config_preemption_bool` — CONFIG_PREEMPTION is 0 or 1
- [ ] `config_timeslicing_bool` — CONFIG_TIME_SLICING is 0 or 1

### 2.2 Constant migration correctness
- [ ] `config_max_tasks_matches_scheduler` — `MAX_TASKS == CONFIG_MAX_TASKS` in scheduler.hpp
- [ ] `config_page_size_matches_pmm` — `pmm::PAGE_SIZE == CONFIG_PAGE_SIZE`
- [ ] `config_page_size_matches_vmm` — `vmm::PAGE_SIZE == CONFIG_PAGE_SIZE`
- [ ] `config_stack_size_matches_task` — `task::STACK_SIZE == CONFIG_STACK_SIZE`
- [ ] `config_vfs_fds_matches` — `vfs::MAX_FDS == CONFIG_MAX_FDS`
- [ ] `config_vfs_mounts_matches` — `vfs::MAX_MOUNTS == CONFIG_MAX_MOUNTS`
- [ ] `config_vfs_path_matches` — `vfs::MAX_PATH == CONFIG_VFS_MAX_PATH`
- [ ] `config_daemons_matches` — `daemon::MAX_DAEMONS == CONFIG_MAX_DAEMONS`
- [ ] `config_drivers_matches` — `driver::MAX_DRIVERS == CONFIG_MAX_DRIVERS`
- [ ] `config_programs_matches` — `MAX_PROGRAMS == CONFIG_MAX_PROGRAMS`
- [ ] `config_signals_matches` — `MAX_SIGNAL_HANDLERS == CONFIG_MAX_SIGNAL_HANDLERS`
- [ ] `config_sync_waiters_matches` — All sync primitives use `CONFIG_SYNC_MAX_WAITERS`

## 3. Syscall Gating Integration — test_syscall_gating.cpp

### 3.1 Dispatch table sizing
- [ ] `config_syscall_count_computed` — `CONFIG_SYSCALL_COUNT` equals actual number of enabled syscalls (compile-time assert)
- [ ] `config_syscall_dispatch_array_sized` — Syscall dispatch table uses `CONFIG_SYSCALL_COUNT` for array size

### 3.2 Conditional compilation
- [ ] `syscall_yield_gated` — `SYS_YIELD` handler behind `#if CONFIG_INCLUDE_SYS_YIELD`
- [ ] `syscall_fork_gated` — `SYS_FORK` handler behind `#if CONFIG_INCLUDE_SYS_FORK`
- [ ] `syscall_open_gated` — `SYS_OPEN` handler behind `#if CONFIG_INCLUDE_SYS_OPEN`
- [ ] `syscall_reboot_gated` — `SYS_REBOOT` handler behind `#if CONFIG_INCLUDE_SYS_REBOOT`
- [ ] `syscall_halt_gated` — `SYS_HALT` handler behind `#if CONFIG_INCLUDE_SYS_HALT`
- [ ] _All 35 syscalls gated_ (list abbreviated — same pattern for all)

### 3.3 Stub handler on disabled
- [ ] `disabled_syscall_returns_enosys` — Calling a gated-off syscall returns `-ENOSYS`
- [ ] `disabled_syscall_does_not_panic` — Disabled syscall entry path doesn't crash (stub handler exists)

## 4. MemPool Configuration Wiring — test_mempool_config.cpp

### 4.1 Dynamic pool table from config
- [ ] `mempool_pool_count_from_config` — `MemPool::POOL_COUNT == CONFIG_MEMPOOL_NUM_POOLS`
- [ ] `mempool_block_sizes_from_config` — `MemPool::block_sizes[i]` matches `CONFIG_MEMPOOL_BLOCK_SIZES` list
- [ ] `mempool_block_counts_from_config` — `MemPool::block_counts[i]` matches `CONFIG_MEMPOOL_BLOCK_COUNTS` list
- [ ] `mempool_total_size_computed` — `MemPool::total_size()` equals sum of `block_size[i] * block_count[i]`

### 4.2 Config change affects allocation behavior
- [ ] `mempool_alloc_size_limits_match_config` — Allocation limits reflect configured pool sizes
- [ ] `mempool_increased_pool_count_works` — With more pools, more size classes available
- [ ] `mempool_decreased_pool_count_still_works` — With fewer pools, allocator still functions

## 5. Hook Configuration Wiring — test_config_hooks.cpp

### 5.1 Weak symbol overrides
- [ ] `idle_hook_invoked_when_enabled` — `CONFIG_IDLE_HOOK=1`, define `idle_hook()`, verify called during idle
- [ ] `tick_hook_invoked_when_enabled` — `CONFIG_TICK_HOOK=1`, define `tick_hook(ticks)`, verify called on timer tick
- [ ] `init_hook_invoked_when_enabled` — `CONFIG_INIT_HOOK=1`, define `init_hook()`, verify called after daemon init
- [ ] `stack_overflow_hook_invoked_when_enabled` — `CONFIG_STACK_OVERFLOW_HOOK=1`, trigger stack overflow, verify hook fires
- [ ] `oom_hook_invoked_when_enabled` — `CONFIG_OOM_HOOK=1`, trigger OOM, verify hook fires

### 5.2 Default weak symbols (no-op)
- [ ] `hooks_not_defined_by_default` — Without user override, weak symbols resolve to empty no-ops
- [ ] `hooks_disabled_by_config_0` — With `CONFIG_*_HOOK=0`, hook invocations compile out entirely

## 6. Feature Flag Wiring — test_config_features.cpp

### 6.1 FPU gating
- [ ] `fpu_context_save_restore_gated` — FXSAVE/FXRSTOR behind `#if CONFIG_HAS_FPU`
- [ ] `fpu_disabled_no_fxsave` — With `CONFIG_HAS_FPU=0`, no FXSAVE/FXRSTOR calls emitted

### 6.2 Hardware RNG gating
- [ ] `rdrand_gated` — RDRAND instruction behind `#if CONFIG_HAS_RDRAND`
- [ ] `rdrand_disabled_fallback` — With `CONFIG_HAS_RDRAND=0`, /dev/random uses ChaCha20 PRNG only

### 6.3 Interrupt controller gating
- [ ] `apic_timer_gated` — `CONFIG_HAS_APIC` gates APIC timer initialization vs PIT fallback
- [ ] `gic_gated` — `CONFIG_HAS_GIC` gates GIC distributor/CPU interface init (aarch64)
- [ ] `plic_gated` — `CONFIG_HAS_PLIC` gates PLIC init (riscv64)
- [ ] `sbi_gated` — `CONFIG_HAS_SBI` gates SBI ecall usage (riscv64)

### 6.4 Future feature gates (no-op defaults)
- [ ] `mpu_not_used` — With `CONFIG_HAS_MPU=0`, MPU code is not compiled
- [ ] `hpet_not_used` — With `CONFIG_HAS_HPET=0`, HPET driver is not compiled

## 7. Scheduling Configuration Wiring — test_config_scheduling.cpp

### 7.1 Idle yield behavior
- [ ] `idle_yield_enabled` — `CONFIG_IDLE_YIELD=1`, idle task calls `SYS_YIELD` instead of busy-wait
- [ ] `idle_yield_disabled` — `CONFIG_IDLE_YIELD=0`, idle task busy-waits (original behavior)

### 7.2 Time slicing
- [ ] `timeslicing_enabled` — `CONFIG_TIME_SLICING=1`, round-robin time-slice preemption active
- [ ] `timeslicing_disabled` — `CONFIG_TIME_SLICING=0`, no round-robin preemption (strict priority)
- [ ] `timeslicing_depends_preemption` — `CONFIG_TIME_SLICING=1` with `CONFIG_PREEMPTION=0` is rejected

### 7.3 Maximum priority
- [ ] `max_priority_shapes_scheduler` — Scheduler priority arrays sized by `CONFIG_MAX_PRIORITY`
- [ ] `max_priority_at_least_priority_ceiling` — `CONFIG_MAX_PRIORITY >= CONFIG_PRIORITY_CEILING`

## 8. Unwired Configs — test_config_unused.cpp

### 8.1 User space limit
- [ ] `user_space_limit_used_in_vmm` — VMM user address ceiling uses `CONFIG_USER_SPACE_LIMIT`
- [ ] `user_space_limit_different_per_arch` — Architecture-specific defaults apply correctly

### 8.2 Heap and stack validation
- [ ] `heap_minimum_validated` — `CONFIG_HEAP_SIZE` >= heuristic minimum enforced at boot
- [ ] `min_stack_size_enforced` — Task creation fails if `requested_stack < CONFIG_MIN_STACK_SIZE`

### 8.3 Task name length
- [ ] `task_name_truncated_to_limit` — Task names longer than `CONFIG_TASK_NAME_LEN` are truncated
- [ ] `task_name_length_0_or_empty` — Empty task name gracefully handled

### 8.4 IPC shared memory
- [ ] `ipc_shmem_max_pages_enforced` — Shared memory allocation capped at `CONFIG_IPC_SHMEM_MAX_PAGES`
- [ ] `process_max_pages_enforced` — Process memory reservation capped at `CONFIG_MAX_PROCESS_PAGES`

## 9. check-config Build Integration — test_config_build_integration.sh

- [ ] `check_config_runs_on_build` — `make` (or `make all`) optionally runs `check-config` when `ENFORCE_CONFIG_CHECK=1`
- [ ] `config_summary_output` — `make config-summary` prints all active CONFIG_* values and exits 0
- [ ] `check_config_ci_compatible` — `make check-config` passes in CI with default config

## 10. Build Regression — test_config_build_variants.sh

- [ ] `build_with_minimal_config` — Build with minimal syscall set, small MemPool, verify binary size reduction
- [ ] `build_with_default_config` — Default config builds and boots (pre-existing, regression check)
- [ ] `build_with_custom_config_file` — `make CONFIG_FILE=my_config.h` picks up custom overrides
- [ ] `build_with_maximal_config` — All features enabled, large pools, verify still boots
- [ ] `build_with_assert_override` — Custom `CONFIG_ASSERT` macro compiles without error

## Files Modified/Added by This Test Suite
- `tools/check-config.py` — add missing validation checks (mempool lists, timeslicing dependency, priority ceiling vs max, arch mutual exclusion)
- `src/kernel/test/test_config_sanity.cpp` — NEW
- `src/kernel/test/test_syscall_gating.cpp` — NEW
- `src/kernel/test/test_mempool_config.cpp` — NEW
- `src/kernel/test/test_config_hooks.cpp` — NEW
- `src/kernel/test/test_config_features.cpp` — NEW
- `src/kernel/test/test_config_scheduling.cpp` — NEW
- `src/kernel/test/test_config_unused.cpp` — NEW
- `Makefile` — add `ENFORCE_CONFIG_CHECK=1` support, wire into build
- `test_config_build_variants.sh` — NEW shell-based test script
- `test_config_build_integration.sh` — NEW shell-based test script
