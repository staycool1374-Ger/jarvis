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

## Session Summary (current v0.2.18-dev → next v0.2.19-dev)

### Completed This Session
- **HAL abstraction**: 14 interface headers in `src/kernel/arch/hal/`; x86-64 impls migrated to `arch/x86_64/hal/`; existing `arch/*.hpp` → thin shims.
- **Multi-arch build**: `ARCH=aarch64`/`riscv64` toolchain blocks; arch-parametric `QEMU_SYSTEM`, `OBJCOPY`, `GDB`, `OBJDUMP`, linker scripts; auto `-DCONFIG_ARCH_*`; `CXXFLAGS_COMMON` factored.
- **Deterministic replay**: `make rr-record` (QEMU `-icount` + `-record`) and `make rr-replay` (replay + GDB stub).
- **PCI debug**: `pci_print_tree(char*, size_t)` in `x86_64/pci.cpp`; `/proc/pci` procfs entry; unblocked test.
- **Healthcheck**: optional `ARCH` argument, per-arch toolchain checks with `brew` suggestions.
- **Bug fix**: `#include <string.hpp>` in `test_pci.cpp`.
- **Version**: v0.2.18 done (Observability & Portability). All committed.

### Next Version (v0.2.19)
- Kernel Memory Safety audit
- Renode simulation setup
