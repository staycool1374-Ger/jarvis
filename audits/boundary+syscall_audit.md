[>] Running Agent 3: Kernel Synthesizer...
# Jarvis RTOS — Verified Kernel Audit Report (ASIL-D Gate)
**Reviewer:** Lead Kernel Architect
**Scope:** syscall layer, VFS core, FAT32/tmpfs/devfs backends, ELF loader
**Disposition:** Filtered attacker report — 12 of 21 claims verified as real defects, 9 dismissed as false positives / unsupported speculation (see Appendix).

---

## VERIFIED FLAWS — ACTION REQUIRED

- [ ] **VULN-C1: Unvalidated raw pointer dereference in `sys_fstat`**
  - **LOCATION:** `src/kernel/syscall/syscall_handlers_fs.cpp`, function `Syscall::sys_fstat`, line:
    ```cpp
    auto *st = reinterpret_cast<vfs::VfsStat *>(arg1);
    return static_cast<uint64_t>(f->vnode->ops->fstat(*f->vnode, *st));
    ```
  - **ROOT CAUSE:** `arg1` is a Ring‑3-controlled 64-bit value dereferenced with zero validation. Every sibling handler in the same file (`sys_stat`, `sys_gettod`, `sys_readdir`) wraps the destination in `checked()` before use. This is an arbitrary-kernel-write primitive callable directly from user space. Violates ASIL-D Freedom-From-Interference (a user task can corrupt kernel/other-partition memory) and the Validation invariant.
  - **REQUIRED FIX:**
    1. Replace the raw `reinterpret_cast` with `auto st = checked(reinterpret_cast<vfs::VfsStat *>(arg1));`
    2. Add `if (syscall_is_user_task() && !st.valid()) return static_cast<uint64_t>(-1);` immediately after.
    3. Call `f->vnode->ops->fstat(*f->vnode, *st.unsafe_ptr())`, mirroring the exact pattern in `sys_stat` in the same file.
    4. No heap allocation required — `checked()` operates on stack-resident wrapper objects only (zero-alloc compliant).

- [ ] **VULN-C2: `sys_ioctl` forwards unchecked Ring-3 pointer to arbitrary device driver**
  - **LOCATION:** `src/kernel/syscall/syscall_handlers_fs.cpp`, function `Syscall::sys_ioctl`:
    ```cpp
    return static_cast<uint64_t>(
        f->vnode->ops->ioctl(*f->vnode, arg1, reinterpret_cast<void *>(arg2)));
    ```
  - **ROOT CAUSE:** `arg2` crosses the user/kernel trust boundary with no `CheckedPtr` validation. Current `VnodeOps::ioctl` implementations are stubs, but the syscall boundary itself is the enforcement point per ASIL-D defense-in-depth — any future driver that dereferences `arg2` inherits an unvalidated pointer with no compensating control.
  - **REQUIRED FIX:**
    1. Introduce a minimum-bound validation at the syscall boundary: if `syscall_is_user_task()` and `arg2 != 0`, wrap as `auto arg_chk = checked(reinterpret_cast<uint8_t *>(arg2), sizeof(uint64_t));` (conservative lower bound for the smallest addressable ioctl payload) and reject with `-1` if `!arg_chk.valid()`.
    2. Extend `VnodeOps::ioctl` signature (freestanding, no exceptions) to accept a `kernel::CheckedPtr<void>` instead of raw `void*`, forcing every current and future backend to explicitly call `.unsafe_ptr()` only after validating the *command-specific* size it actually needs. Update all `*_ioctl` stub implementations across `devfs.cpp`, `fat32_fs.cpp`, `pipe.cpp`, `procfs.cpp`, `tmpfs.cpp`, `initrd_fs.cpp` to match the new signature (they all currently return `VFS_INVALID` unconditionally — signature-only change, zero logic/alloc impact).

- [ ] **VULN-C4: TOCTOU between `vfsd_authorize` and path re-resolution in all path syscalls**
  - **LOCATION:** `src/kernel/syscall/syscall_handlers_fs.cpp`, functions `sys_open`, `sys_stat`, `sys_mkdir`, `sys_unlink`, `sys_rmdir`, `sys_chdir` — pattern:
    ```cpp
    if (!vfsd_authorize(vfsd::VFS_OPEN, ..., path_buf)) return -1;
    fd = syscall_path_open(path_buf, arg1);   // re-resolves path independently
    ```
    and `vfsd_authorize` in the same file, which calls `IPC::send_sync(vfsd_pid, send_msg, reply_msg)` — a **blocking** call that yields the CPU to other tasks.
  - **ROOT CAUSE:** `vfsd_authorize` blocks on synchronous IPC (scheduler yields during the wait). `vfs::resolve()` (see `src/kernel/vfs/vfs.cpp`) performs unlocked mount-table/vnode-tree traversal. Between the authorization decision and the actual filesystem operation, a concurrent task can `unlink`/`mkdir`/re-mount the same path, causing the operation to act on a different object than the one authorized. This is a classic authorization-bypass TOCTOU, unacceptable for ASIL-D access-control guarantees.
  - **REQUIRED FIX:**
    1. Change ordering: call `vfs::resolve(path_buf)` **first** to obtain the target `vfs::Vnode*` and capture its `ino` (inode number) locally on the stack.
    2. Extend `vfsd::Msg` (already fixed-size, no allocation) to carry the captured `ino` alongside the path string, so vfsd authorizes against the *specific resolved object*, not just the path string.
    3. After the `vfsd_authorize` IPC round-trip returns success, re-resolve the path a second time and compare `resolved_vnode->ino` (and filesystem instance pointer) against the value captured in step 1. If they differ, fail the syscall with `-1` (object identity changed during authorization window).
    4. Perform the actual operation (`syscall_task_open`, `vfs::mkdir`, `vfs::unlink`, etc.) using the **already-resolved vnode pointer** from step 3, not by re-resolving a third time.
    5. All buffers involved (`path_buf`, `vfsd::Msg`) remain fixed-size stack arrays — no heap allocation introduced.

- [ ] **VULN-C5/C6: Unsynchronized `vnode->refcount` and `FdTable` mutation across `sys_chdir`, `sys_dup`, `sys_dup2`, and `FdTable::free`**
  - **LOCATION:**
    - `src/kernel/syscall/syscall_handlers_fs.cpp`, `sys_chdir`: `--cur->cwd_vnode->refcount;` / `++vn->refcount;`
    - `src/kernel/syscall/syscall_handlers_fs.cpp`, `sys_dup`: `++old->vnode->refcount;`
    - `src/kernel/syscall/syscall_handlers_fs.cpp`, `sys_dup2`: `++old_desc->vnode->refcount;`
    - `src/kernel/vfs/vfs.cpp`, `FdTable::free` / `FdTable::free_err`: unguarded `--fds[fd].vnode->refcount` and conditional `close()`.
  - **ROOT CAUSE:** `Vnode::refcount` is a plain `int` mutated with non-atomic `++`/`--` from multiple syscall paths with no spinlock/mutex. A preemption between decrement and the `refcount == 0` check in `FdTable::free` (or a concurrent `dup`/`close` pair) causes a lost update: either premature `ops->close()` while a live reference exists (use-after-free) or a refcount leak (resource exhaustion) — both are certifiable ASIL-D freedom-from-interference violations.
  - **REQUIRED FIX:**
    1. Change `Vnode::refcount` type from `int` to `std::atomic<int>` (freestanding `<atomic>` is available; no allocation implications — same footprint, lock-free intrinsics on the target ISA).
    2. Replace all `++vn->refcount` / `--vn->refcount` with `vn->refcount.fetch_add(1, std::memory_order_relaxed)` / `.fetch_sub(1, std::memory_order_acq_rel)`.
    3. In `FdTable::free`/`free_err`, use the *return value* of `fetch_sub` (previous value) to decide whether this call was the one that brought the count to zero (`prev == 1`), eliminating the read-then-branch race entirely — do not re-read `refcount` after decrementing.
    4. `cur->cwd_vnode` swap in `sys_chdir` must additionally be guarded: introduce a per-task `sync::SpinLock cwd_lock_` member on `TaskControlBlock` (already a fixed-layout struct — zero-alloc) and acquire/release it around the `cwd_vnode` read-modify-write sequence in `sys_chdir`.

- [ ] **VULN-H1: ELF loader maps all `PT_LOAD` segments with uniform permission bits, defeating W^X enforcement**
  - **LOCATION:** `src/kernel/elf/elf.cpp`, function `load_segments_and_stack`:
    ```cpp
    VMM::map_page_in_pml4(vaddr_base + page_idx * arch::PAGE_SIZE,
                          seg_phys + page_idx * arch::PAGE_SIZE, true, pml4);
    ```
    (same uniform `true` literal reused for the user stack and heap mappings later in the same function).
  - **ROOT CAUSE:** `validate_segment()` correctly rejects an ELF program header that declares both `PF_W` and `PF_X` (line: `if ((phdr->flags & 2) && (phdr->flags & 1)) return false;`), but this check is purely nominal — the actual page-table mapping call never consults `phdr->flags` at all. Every segment (code, rodata, data, plus the stack and heap, which are never executable by intent) is mapped with the identical boolean. If `map_page_in_pml4`'s third parameter does not independently set the NX bit, the user stack and heap are executable in practice, enabling classic stack/heap shellcode execution on any subsequent buffer-overflow bug — an unacceptable compounding of severity for ASIL-D memory-protection requirements.
  - **REQUIRED FIX:**
    1. Extend `VMM::map_page_in_pml4` (or add an overload) to take an explicit permission bitmask, e.g. `enum class PageFlags : uint8_t { Writable = 1, Executable = 2 };`, replacing the single `bool`.
    2. In `load_segments_and_stack`, derive per-segment flags from `phdr->flags`: `Writable` iff `phdr->flags & PF_W(0x2)`, `Executable` iff `phdr->flags & PF_X(0x1)`. Pass these explicitly per `PT_LOAD` segment instead of the literal `true`.
    3. For the user stack and heap mappings (later in the same function), pass `PageFlags::Writable` **only** — never `Executable` — unconditionally, regardless of any legacy call-site default.
    4. Ensure the underlying PTE-writing code sets the architectural NX bit (x86_64: bit 63) whenever `Executable` is not requested. This is a pure bit-manipulation change with no dynamic allocation.

- [ ] **VULN-H2: Out-of-bounds read of ELF file buffer via unchecked `phdr->offset + phdr->filesz` against actual file size**
  - **LOCATION:**
    - `src/kernel/syscall/syscall_handlers_process.cpp`, `Syscall::sys_exec` (allocates `file_buf` sized to `vn->size`, ≤ 512 KiB).
    - `src/kernel/elf/elf.cpp`, function `load_segments_and_stack`:
      ```cpp
      memcpy(reinterpret_cast<void *>(arch::HHDM_OFFSET + seg_phys + offset_in_region),
             file_data + phdr->offset, phdr->filesz);
      ```
    - `src/kernel/elf/elf.cpp`, `validate_segment` (only bounds `phdr->offset > 4_MiB`, never against the real file size).
  - **ROOT CAUSE:** `validate_segment` has no knowledge of the actual loaded file length; it only bounds `offset` to an arbitrary constant (4 MiB) and `memsz` to 64 MiB, both far larger than the 512 KiB buffer allocated in `sys_exec`. A crafted ELF can set `phdr->offset` near the end of the real file and a large `filesz`, causing `memcpy` to read past the `PMM::alloc_contiguous(file_pages)` buffer — leaking adjacent physical kernel/free-pool memory directly into the new process's mapped segment. This is a confirmed cross-domain information-disclosure primitive reachable via `exec()` from Ring 3.
  - **REQUIRED FIX:**
    1. Thread the actual validated file size through the call chain: change `validate_segment(const ELF64ProgramHeader *phdr)` to `validate_segment(const ELF64ProgramHeader *phdr, uint64_t file_size)`.
    2. Add the check `if (phdr->offset + phdr->filesz > file_size) return false;` (perform the addition with overflow check already present, but add the file_size comparison **after** the overflow check).
    3. Update `load_segments_and_stack` signature to accept `uint64_t file_size` and pass it through from `sys_exec` (`vn->size`, or the actual bytes returned by `r` from `vn->ops->read`, whichever is authoritative — use `static_cast<uint64_t>(r)`).
    4. No new allocation — this is a parameter-threading and comparison-only change.

- [ ] **VULN-H4/W1: Unbounded string/array scan in `validate_argv_envp` bypasses its own single-byte validation**
  - **LOCATION:** `src/kernel/syscall/syscall_handlers_process.cpp`, function `validate_argv_envp`:
    ```cpp
    auto s = checked(*p, static_cast<size_t>(1));
    if (!s.valid()) return false;
    size_t len = 0;
    while (s.unsafe_ptr()[len])  // unbounded scan past the validated 1 byte
        ++len;
    ```
    and the enclosing `while (*p)` loop with no cap on the number of `argv`/`envp` entries.
  - **ROOT CAUSE:** `checked(*p, 1)` only certifies that *one byte* at `*p` is mapped and accessible; the subsequent NUL-terminator scan walks arbitrarily far past that single validated byte with no re-validation, no page-boundary check, and no length cap. A malicious task can place a non-NUL-terminated string abutting an unmapped page to trigger an unvalidated kernel-side page fault (DoS) or, combined with U2 below, corrupt kernel memory. The unbounded `while (*p)` entry-count loop is additionally an unbounded-duration operation inside a syscall handler — a direct WCET violation for a hard real-time kernel (`sys_exec` has no deadline enforcement on this loop).
  - **REQUIRED FIX:**
    1. Define compile-time constants: `constexpr size_t MAX_EXEC_ARGS = 64;` and `constexpr size_t MAX_EXEC_ARG_LEN = SYSCALL_MAX_PATH;` (or an equivalent fixed bound already used elsewhere in the codebase).
    2. In the `while (*p)` loop, add an explicit counter and `if (++arg_count > MAX_EXEC_ARGS) return false;`.
    3. Replace the unbounded NUL-scan with a **bounded, validated** scan: call `auto s = checked(*p, MAX_EXEC_ARG_LEN);` up front (validating the *entire maximum-length window*, not just 1 byte), then scan `for (size_t len = 0; len < MAX_EXEC_ARG_LEN; ++len) { if (s.unsafe_ptr()[len] == '\0') break; if (len == MAX_EXEC_ARG_LEN - 1) return false; }` — i.e., every byte touched must first fall within a range already certified valid by `checked()`.
    4. Track and return the total accumulated string length (`argv` + `envp` combined) from `validate_argv_envp` via an out-parameter, for use by VULN-U2's fix below. No heap allocation — all bounds are compile-time constants and stack counters.

- [ ] **VULN-U2: Unbounded stack-pointer underflow in `setup_user_stack` from attacker-controlled `argv`/`envp` total length**
  - **LOCATION:** `src/kernel/elf/elf.cpp`, function `setup_user_stack`:
    ```cpp
    uint64_t str_total = total_string_len(argv) + total_string_len(envp);
    ...
    uint8_t *sp = stack_top;
    sp -= str_total;   // no bound check against allocated stack region
    ```
  - **ROOT CAUSE:** `sp` walks backward from the top of the freshly-allocated user stack (`mem::STACK_SIZE` bytes) by `str_total`, which is only indirectly bounded today by the (broken, per VULN-H4) `validate_argv_envp`. If `str_total` exceeds `mem::STACK_SIZE`, `sp` underflows below the allocated physical pages and the subsequent `copy_strings()` writes corrupt adjacent physical memory outside the new process's stack allocation — a kernel-reachable, user-triggerable memory-corruption primitive via `exec()`.
  - **REQUIRED FIX:**
    1. After computing `str_total` in `setup_user_stack`, add a hard reservation check before any pointer arithmetic: `constexpr uint64_t kStackReserve = 512;` (room for argv/envp pointer arrays + alignment) and `if (str_total + kStackReserve >= mem::STACK_SIZE) return 0;` — propagate a `0`/failure sentinel up through `exec_into_current`/`load` to abort the exec/load with an error instead of proceeding.
    2. Update both call sites (`load()` and `exec_into_current()`) in `elf.cpp` to check the return value of `setup_user_stack` for `0` and fail cleanly (`return nullptr;` / `return false;`) rather than assuming success.
    3. This must be enforced **in addition to** the `MAX_EXEC_ARGS`/`MAX_EXEC_ARG_LEN` caps from VULN-H4 — the two fixes are complementary defense-in-depth layers, not substitutes for one another.

- [ ] **VULN-W2: Unbounded busy-wait (`UINT64_MAX` spin loop) in `tty_read`/`kbd_read` violates WCET without descheduling**
  - **LOCATION:** `src/kernel/vfs/devfs.cpp`, functions `tty_read` and `kbd_read`:
    ```cpp
    for (uint64_t retry = 0; retry < UINT64_MAX; ++retry) {
        ...
        arch::pause();
    }
    ```
  - **ROOT CAUSE:** In blocking mode (no `O_NONBLOCK`), this loop spins on `arch::pause()` without ever calling `Scheduler::reschedule()` or yielding the CPU, unlike the correct blocking pattern already established in `Syscall::sys_receive` (`syscall_handlers_ipc.cpp`), which properly reschedules and halts. A task calling blocking `read()` on `/dev/tty` or `/dev/kbd` monopolizes its CPU core indefinitely, starving every other task scheduled on that core — a direct, demonstrable WCET/schedulability violation for a hard real-time kernel.
  - **REQUIRED FIX:**
    1. Replace the `arch::pause()` spin with the same cooperative pattern used in `sys_receive`: on each iteration where no character is available, set `Scheduler::current_task()->state = TaskState::BLOCKED;`, call `Scheduler::reschedule();`, and only re-enter the polling check after control returns to this task.
    2. If the target architecture has an interrupt-driven keyboard/serial-data-ready notification already available (per `arch::Keyboard`/serial IRQ handlers), prefer converting this into a proper wait/notify blocking primitive (e.g., a `sync::Semaphore` posted from the IRQ handler) rather than a polled reschedule loop — this is the preferred fix if the IRQ infrastructure exists; the reschedule-loop is the minimum-viable fix otherwise.
    3. No dynamic allocation is required for either fix — `TaskState` and `Scheduler` are already fixed, freestanding kernel objects.

- [ ] **VULN-W3: `sys_receive` has no bounded/timeout blocking variant**
  - **LOCATION:** `src/kernel/syscall/syscall_handlers_ipc.cpp`, function `Syscall::sys_receive`:
    ```cpp
    while (!(ok = IPC::recv(msg))) { ... Scheduler::reschedule(); ... }
    ```
  - **ROOT CAUSE:** The blocking mechanics themselves are correct (properly yields via `Scheduler::reschedule()`/`hlt`), but the syscall exposes no deadline/timeout argument. For ASIL-D certification, every blocking kernel primitive reachable by a task with a real-time deadline must have a bounded-wait variant; an indefinite blocking receive is a liveness/WCET analysis gap at the API level.
  - **REQUIRED FIX:**
    1. Add a new syscall parameter (reuse the currently-unused `arg3` slot in `sys_receive`'s signature) as `uint64_t timeout_ticks` (0 = block forever, preserving current behavior for non-real-time callers).
    2. In the `while (!(ok = IPC::recv(msg)))` loop, capture `uint64_t deadline = timeout_ticks ? arch::Timer::ticks() + timeout_ticks : 0;` before the loop, and add `if (deadline && arch::Timer::ticks() >= deadline) return static_cast<uint64_t>(-1);` as an additional loop-exit condition, checked once per iteration before rescheduling.
    3. No new state/allocation required — `arch::Timer::ticks()` is already used elsewhere in this exact file for the sporadic-server logic.

---

## APPENDIX — DISMISSED AS FALSE POSITIVE / UNSUPPORTED (no action)

| ID | Reason for dismissal |
|---|---|
| C3 | Attacker self-dismissed; no finding. |
| H3 | Architectural observation about `readdir` trust centralization; no concrete OOB write demonstrated in any of the five listed backends — all bound writes to `dent.d_name` with explicit `idx < 63`/`sizeof(...)-1` guards. |
| H5 | Kernel-task (`!syscall_is_user_task()`) bypass is the codebase's explicit, documented trust model ("Kernel tasks (no page table) are trusted"), consistently applied across `vfsd_authorize`/`vfsd_authorize_fd_op`. Not a flaw without evidence of a concrete untrusted-input path reaching this branch. |
| M1 | Speculative; `syscall_table_` definition/population is not in the reviewed file set — no evidence of sparse initialization. |
| M2 | Speculative; `Message::data` bound is defined outside the reviewed files — no evidence of a mismatch. |
| M3 | Self-admitted "not a hard bug" by the attacker; consistent with the established kernel-task trust model. |
| M4 | Attacker self-dismissed; no finding. |
| U1 | Attacker self-dismissed; explicitly out of scope for this file set. |
| W1 (as originally scoped to argv only) | Merged into VULN-H4 above rather than dismissed — not a duplicate deduction, just consolidated under one action item since both stem from the same root cause in `validate_argv_envp`. |
