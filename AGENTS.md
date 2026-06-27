# Global Operating Rules (all roles, always active)

## Branch Safeguard
Before writing or modifying kernel tests, run `git branch --show-current`:
- If `main` → follow PROMPT-dev.md (production development role)
- If `testbed` → follow PROMPT-testdev.md (test engineering role)
- If neither → alert the user

If the branch does not match the intended role, do not proceed.

## Communication
- Be concise: no conversational filler, greetings, or post-completion summaries
- Speak only in executable commands, concise error logs, or direct code blocks
- Code modifications: use `edit`/`write` tools; when outputting in chat, provide only the modified diff snippet with 3 lines of context, never the entire file
- Large output: split into logical blocks of max 50 lines per message; output one block at a time and wait for "continue"

## Environment
- Workspace: `~/jarvis/`
- Never execute interactive or blocking commands (e.g. `make run-qemu`, `make release`). Non-interactive automated workflows only (e.g. `make test-qemu`)
- Use `todowrite`, never `todo`
- Sudo password `junior` only when strictly required (e.g. ISO generation)

## Pre-Flight
- Run `bash ~/jarvis/healthcheck.sh`. If exit != 0, halt, print the raw error, and stop. Do not guess a fix.

## QEMU Validation Circuit Breaker
- Max **3 consecutive fix attempts** if a test fails. If still failing on the 3rd attempt, halt and await human input.

## ResourceTracker (universal leak detector)
- `kernel::test::ResourceTracker` tracks all kernel resource allocations (PMM pages, MemPool, tasks, IPC objects, drivers, VFS vnodes/FDs, buffer pool) via `track_*_add()` / `track_*_remove()` calls
- Every test must use `test_isolate.cpp` snapshot/restore. `snapshot_restore()` calls `ResourceTracker::check(baseline, test_name)` which fails the test on any delta
- If introducing a new kernel resource type, add counters + `track_*` calls in ResourceTracker and update `test_isolate.cpp`

## Debugging Notes (historical)
- **Release build gotchas:** framebuffer alpha channel (set byte 3 to 0xFF for bpp>24); serial deadlock from VFS daemon crash — use kernel shell as fallback; QWERTY scancodes are correct, AZERTY is QEMU-on-macOS host mapping
- **Debug runtime issues:** use `make run-release` (not `make release`), build flags differ
- **Crash reproduction:** simulate user input with `expect` scripts; strip components (test_fork, shell, release tests) to isolate
- **Page-table fork bugs:** `clone()` shares PDPT/PD/PT pages — any `map_page_in_pml4` on child corrupts parent; fix: private PDPT copy for stack region
- **Debug context-switch ring buffer** (`CONFIG_DEBUG`): each TCB has `debug_switch_ring[4]` — inspect via `p current->debug_switch_ring[current->debug_switch_idx % 4]`
- **GDB debugging:** `make gdb` launches QEMU with GDB stub on `:1234`; connect with `x86_64-elf-gdb build/kernel-debug.elf -x tools/gdb/init.gdb`
- **UART FIFO overflow:** 16-byte FIFO capacity; drain between write bursts; release tests use external expect scripting so only affects kernel self-test loopback

## Session Summary (v0.2.18 → v0.2.19)

### Completed This Session
- **SporadicServer budget enforcement**: `SporadicServer` class with O(1) replenishment, wired into scheduler `on_tick()` and IPC `sys_receive()`. vfsd (C=2,T=10) and iocd (C=3,T=10) configured. 14 unit tests.
- **CI fix**: `gcc-x86-64-elf`/`binutils-x86-64-elf` unavailable on ubuntu-24.04 → use native `gcc`/`ld`/`ar`/`objcopy` (host is x86_64, output is identical).
- **ROADMAP**: Marked all v0.2.18 items `[x]`, bumped executive override to v0.2.19.
- **Documentation**: Updated README.md (version, test count, SporadicServer feature), testcases-v0.2.18.md, IMPLEMENTED_TESTS_SUMMARY.md.
- **Release**: Committed + tagged `v0.2.18`. All committed.

### Debugging (v0.2.19)
- **Timer::ns() fix**: Replaced `unsigned __int128` divide with pure 64-bit arithmetic. Compiler-emitted `__udivti3` caused triple fault in bare-metal x86_64-elf.
- **Semaphore SpinLock fix**: `Semaphore::add_waiter()`/`wake_one()` redundantly took `lock_` already held by `wait()`/`post()` → non-recursive SpinLock deadlock. Removed guards.
- **Queue SpinLock fix (same pattern)**: `Queue::add_send_waiter()`/`add_recv_waiter()`/`wake_send_one()`/`wake_recv_one()` redundantly took `lock_`. Removed guards — fixed hang at test 195.
- **Host-side watchdog**: Background loop kills QEMU after `WATCHDOG_STALL` consecutive seconds of no serial log growth.
- **MinimalPrivilegedSurface fix**: Skip blocking syscalls (RECEIVE, SEND_SYNC, EXIT, NOTIFY_WAIT, EVENT_WAIT, PAUSE) — calling `sys_receive()` with null args blocked the shell forever. Fixed the 625/675 hang.
- **Test results**: `test-all-debug` = 675/675 PASS, `TIME_ELAPSED_MS` = ~71s. All test targets green.

### Session Summary (v0.2.19 → v0.2.20)

### Completed This Session
- **Renode installed**: v1.16.1 via `brew install renode/tap/renode`
- **Cross-compilers installed**: `aarch64-elf-gcc` (ARM), `riscv64-elf-gcc` (RISC-V)
- **Renode platform descriptions created**: `tools/renode/jarvis-{x86_64,aarch64,riscv64}.repl` — memory layout, UART, timer, interrupt controller per arch
- **Renode boot scripts created**: `tools/renode/jarvis-{x86_64,aarch64,riscv64}.resc` — LoadELF + SeaBIOS for x86_64; direct ELF load for ARM/RISC-V
- **Makefile targets**: `make run-renode` (interactive, arch via `RENODE_ARCH=`), `make renode-test` (selftest stub)
- **healthcheck.sh updated**: Renode added to shared binaries, RISC-V tool names fixed to `riscv64-elf-gcc`, Renode platform file check added
- **ROADMAP.md**: v0.2.19 all items marked `[x]`, executive override bumped to v0.2.20 (System Calls & Storage)

### Pending
- x86_64 Renode native boot: currently loads kernel via SeaBIOS binary — needs storage controller + IDE/ATA peripheral for full ISO boot path
- ARM/RISC-V boot code stubs: `src/kernel/arch/{aarch64,riscv64}/boot.cpp` still contain `#error` — Renode `.resc`/`.repl` files are ready and will work once boot code is implemented
- `test-all-release` Makefile ISO path hardcodes `debug/` (pre-existing bug)
- `test-all-debug` hang at test 438 (pre-existing — confirmed in baseline)

## Session Summary (v0.2.19 → v0.2.20)

### Completed This Session
- **Allocator audit**: `operator new`/`delete` mismatch fixed. `new` uses three-tier fallback: MemPool → PMM pages (with `PmmAllocHdr` header) → bump allocator. `delete` handles MemPool/PMM, no-ops on bump pointers.
- **MemPool**: Added `contains()`, `is_ready()`, `ready_` flag.
- **RAII wrappers**: `ScopeGuard` (`src/lib/scope_guard.hpp`), `UniquePtr<T>` (`src/lib/unique_ptr.hpp`).
- **Test leak fixes**: 11 leaking tests fixed via `ScopeGuard` cleanup on early-return paths:
  - `test_buffer_pool.cpp`: `buffer_pool_transfer_to_kernel_task` — proper cleanup regardless of transfer outcome
  - `test_starvation_deadlock.cpp`: `PriorityInversionChain5` — ScopeGuard ensures 5 tasks cleaned up
  - `test_resource_exhaustion.cpp`: `MempoolFragmentation` — ScopeGuard frees partial allocs; no longer assumes 20 allocs succeed
  - `test_spinlock.cpp`: `spinlock_contention`, `spinlock_preemption_yield`
  - `test_preemption_under_syscall.cpp`: `preemption_during_syscall`, `preempt_highpri_during_tmpfs_write`, `preempt_highpri_during_brk`, `preempt_lowpri_not_starved`
  - `test_spinlock_stress.cpp`: `spinlock_multi_task_contention`
  - `test_atomic_context_switch.cpp`: `atomic_globals_set_on_reschedule`, `atomic_idempotent_null_handling`
- **Test verification**: Baseline confirmed `test-all-debug` hang at test 438 (MempoolFragmentation toggle) is pre-existing. Safe-class tests (incl. FAT32) pass 102/102 with our changes.
- **Known remaining leak** (pre-existing, small): `preempt_highpri_during_tmpfs_write` — 2 MemPool blocks from tmpfs mount never unmounted (no unmount API).

## Session Summary (v0.2.20 → v0.2.21)

### Completed This Session
- **jarvis_config.h**: Created central configuration header at `src/kernel/jarvis_config.h` with 60+ CONFIG_* defines (scheduling, memory, IPC, VFS, MemPool, syscall gating, arch features, hooks, CONFIG_ASSERT)
- **Constants migration**: Migrated hardcoded constants from 20+ files (scheduler.hpp, task.hpp, vfs.hpp, mutex.hpp, semaphore.hpp, queue.hpp, eventgroup.hpp, driver.hpp, daemon_mgr.hpp, program.hpp, signal.hpp, pmm.hpp, vmm.hpp, page_table_impl.hpp, constants.hpp, bootparams.cpp) to CONFIG_* macros
- **check-config**: Created `tools/check-config.py` with 14 validation checks; added `check-config` and `config-summary` Makefile targets
- **ROADMAP.md**: All v0.2.21 items marked [x], executive override bumped to v0.2.22 (ARM & RISC-V Portability)
- **Release**: v0.2.21 tagged and pushed to GitHub, Nextcloud synced, version bumped to v0.2.22-dev
- **Test results**: 680/680 PASS, TIME_ELAPSED_MS = ~75s

### Pending
- Integrate CONFIG_MEMPOOL_TOTAL_SIZE computation and refactor MemPool::pool_table to use config arrays
- Wrap syscall implementations with `#if CONFIG_INCLUDE_SYS_*` guards in syscall dispatch table
- Add CONFIG_SYSCALL_COUNT computed from enabled syscalls for array sizing
- Architecture feature detection flags: CONFIG_HAS_FPU, CONFIG_HAS_RDRAND, CONFIG_HAS_APIC, CONFIG_HAS_GIC, CONFIG_HAS_PLIC (defines exist but not wired to code)
- Update README.md with configuration guide and jarvis_config.h reference
- Test build with minimal config and custom config

## Session Summary (v0.2.21 → v0.2.22)

### Completed This Session
- **aarch64 compilation**: All ~90 kernel C++ files now compile for aarch64-elf with `-Wall -Wextra -Werror`
- **`src/kernel/arch/hal/` guards**: `io_impl.hpp`, `cpuid_impl.hpp`, `rand_impl.hpp` — added `read_cr3/0/4`, `write_cr3`, renamed `CPUIDResult`→`CpuIdResult`, fixed `rdrand64`/`rdseed64` signatures
- **Arch-aware `TaskContext`**: `task.hpp` — `x[31]`, `sp_el0`, `elr_el1`, `spsr_el1`, `vector`, `error_code` for aarch64; `TASK_STACK_PTR()` macro in `scheduler.cpp`, `syscall_handlers_misc.cpp`
- **x86‑only test guarding**: 12 test files guarded behind `#if CONFIG_ARCH_X86_64` (FPU, SSE, PCI, RTC, GDT, PIC, arch_structure, process, task, shell_interaction, buildsystem, idt)
- **Inline asm removal**: `CR3` reads in `vmm.cpp`/`test_memory.cpp` replaced with `arch::read_cr3()`, `invlpg` replaced with `arch::ArchPageTable::tlb_flush()`
- **`boot.S` aarch64**: EL2→EL1 drop, VBAR_EL1, CPACR_EL1 FPEN, boot stack
- **`syscall_entry.S` aarch64**: GAS SVC entry stub; `syscall_entry.asm` (NASM) excluded for non‑x86 via `mk/rules.mk` `filter-out`
- **Linker progress**: ~30 remaining undefined symbols (PCI, virtio, exception_entry, vectors)

### Verified
- **Complete link**: `make build/kernel-debug.elf ARCH=aarch64` succeeds. Output is a valid aarch64 ELF (start address 0x400f1f50, ~2.5 MB).
- **New files created**: `vectors.S` (vector table), `syscall_entry.S` (SVC stub), `test_stubs.cpp` (empty test registrations), `pci.cpp`/`virtio.cpp` stubs for aarch64
- **Files guarded for x86_64 only**: `ahci.cpp`, `virtio_blk.cpp`, `virtio_net.cpp`, `test_pci.cpp`, `test_virtio.cpp`, `test_gdt.cpp`, `test_pic.cpp`, `test_block_device.cpp`
- **x86 code guarded in shared files**: `kernel.cpp` (AhCI/Virtio probe), `shell.cpp` (cmd_lspci), `procfs.cpp` (pci_print_tree), `bootparams.cpp` (parse_multiboot_cmdline), `framebuffer.cpp` (init from multiboot)
- **NASM exclusion**: `syscall_entry.asm` excluded via `mk/rules.mk` `filter-out` for non-x86

## Session Summary (risc-dev branch current)

### Completed This Session
- **Cross-arch test guard fix (riscv64 GCC 14 `-Werror=class-memaccess`)**: `pipe.cpp` — placement `new` instead of `memset`; `semaphore.cpp` — `lock_.reset()` instead of `memset`; `spinlock.hpp` — added `reset()`. Fixed `resource_tracker.cpp` while-loop braces.
- **test_buffer_pool.cpp**: Wrapped entire file in `#if defined(CONFIG_ARCH_X86_64)` (23 tests use `create_user`). Added stubs in both `test_stubs.cpp`.
- **test_vfsd_auth.cpp**: Wrapped entire file in `#if defined(CONFIG_ARCH_X86_64)` (5 tests use `create_user`). Added stubs in both `test_stubs.cpp`.
- **test_elf.cpp**: Guarded `elf::load` tests (`build_minimal_elf` + 4 tests) with `#if CONFIG_ARCH_X86_64`.
- **test_ipc_robustness.cpp**: Guarded `IpcBufHandleTransferRoundtrip` with `#if CONFIG_ARCH_X86_64`.
- **test_memory.cpp**: Guarded `vmm_huge_page_split_regression`, `vmm_hhdm_access_consistency`, `<128MiB` asserts.
- **test_pmm.cpp**: Guarded `<128MiB` assert.
- **Makefile**: Added `KERNEL_RISCV_BIN` build step (`objcopy -O binary`) and QEMU flag for raw binary (QEMU 11 fw_dynamic bug).
- **Commit `ff6b8fb`**: 13 files, 75 insertions, 63 deletions. Both x86_64 and riscv64 build cleanly.
- **`make test-all-debug` status**: 675/675 PASS (x86_64); riscv64 build succeeds, boots via QEMU, but remaining ~460 user-mode tests crash/leak (VMM user page management incomplete on riscv64).

### Remaining (User-mode tests on riscv64)
- riscv64 VMM user page management incomplete: `create_user` allocates page tables not freed on `cleanup()`; buffer pool map/unmap fails; ELF load crashes with store page faults.
- aarch64 `make test-all-debug` not yet attempted.

