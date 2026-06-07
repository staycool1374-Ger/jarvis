# Lessons Learned

## 2026-06-07 — fork/exec Crash Debugging (SSE + Stack Alignment + Identity Map)

### CR4.OSFXSR must be set for SSE
- GCC at `-O2` emits SSE instructions (`movdqa`, `movaps`) for struct copies, string ops, etc.
- Without `CR4.OSFXSR` (bit 9), SSE instructions raise `#UD` (SIGILL, vector 6).
- `CR4.OSXMMEXCPT` (bit 10) should also be set for SIMD floating-point exception handling.
- Fix: `or dword [cr4], (1<<5)|(1<<9)|(1<<10)` in `boot.asm` (PAE|OSFXSR|OSXMMEXCPT).

### x86-64 Stack Alignment (16-byte)
- ABI requires `(%rsp + 8) ≡ 0 (mod 16)` at function entry — i.e. RSP must be 16-byte aligned **before** `call`.
- The kernel boot stack is set up correctly, but `_start` in `crt0.S` calls `main` without aligning RSP.
- GCC's `movaps [rsp+0x20]` (emitted at `-O2`) faults on misaligned address → `#GP(0)`.
- Fix: `andq $-16, %rsp` before `call main` in `crt0.S`.

### Physical addresses are not valid under user page tables
- `clone_kernel_pml4()` zeroes PML4 entries 0–255 (identity map) and copies only entries 256–511 (higher half).
- Any kernel code dereferencing a physical address directly (`reinterpret_cast<T*>(phys)`) relies on the identity map — which is **absent** in every user task's PML4.
- All physical-to-virtual conversions must use `0xFFFF800000000000 + phys`.
- Affected components: PMM bitmap, VMM page table walks (`get_table`, `map_page_in_pml4`, `free_user_pages`, `clone_kernel_pml4`), ELF loader segment copies.

### Fork's page table isolation is incomplete
- `clone()` creates a new PML4 via `clone_kernel_pml4()` but only maps the new **user stack** in it.
- The program's code (.text), read-only data (.rodata), data (.data), BSS, and heap segments are **not** mapped in the child's PML4.
- The child page-faults immediately upon executing any instruction after fork.
- Fix options:
  - **Share + don't free**: Copy all PML4 entries (0–511) in `clone_kernel_pml4()`, and never call `free_user_pages()` in `exec_into_current()` or `cleanup()`. Leaks but avoids corruption.
  - **Deep-copy**: Recursively copy the user page table hierarchy in `clone()`. Proper isolation, but more code.
  - **Currently chosen**: Share (copy all entries), with the accompanying leak risk. Acceptable because child always execs or exits quickly.

### free_user_pages in exec/exit corrupts shared page tables
- If parent and child share PDPT/PD/PT pages, and the child calls `exec_into_current()` or `cleanup()`, `free_user_pages()` frees the shared page table hierarchy — corrupting the parent's address space.
- Workaround: Skip `free_user_pages()` for tasks that inherited shared page tables.

### Page fault error code layout
| Bit | Meaning |
|-----|---------|
| 0   | 0 = non-present page, 1 = protection violation |
| 1   | 0 = read, 1 = write |
| 2   | 0 = supervisor, 1 = user mode |
| 3   | 1 = reserved bit violation |
| 4   | 1 = instruction fetch |
- Error code `0x4` → user-mode read from non-present page.

### TLB behaviour on CR3 change
- Writing CR3 **flushes all TLB entries** (except global pages marked with `PAGE_GLOBAL`).
- No manual `invlpg` needed after CR3 switch during context switch.

### Fork return value convention
- Parent gets `child->id` from the fork handler (stored in `regs[0]`).
- Child gets `RAX = 0` from the cloned register frame (set in `clone()`).
- Both sides must see the correct split.

### User stack must be per-task in fork
- Fork allocates a **new** user stack for the child and copies the parent's stack content page-by-page via the higher-half mapping.
- The child's cloned PML4 maps this new stack at the same virtual address as the parent's.

### waitpid return semantics
- If `waitpid` is called before the child terminates, the parent blocks. When the child exits, the scheduler must wake the parent.
- The current `WAITPID` handler returns `-1` if the child is still alive on first check, then the parent blocks and gets rescheduled. On re-entry after wakeup, the handler **may not re-check** the child state — can return `-1` incorrectly.
- Fix needed: always re-check child termination state on resume.

### Higher-half kernel mapping
- Kernel is mapped at `0xFFFF800000000000` (PML4 entry 256).
- Physical address `X` → virtual `0xFFFF800000000000 + X`.
- Accessing physical memory without this offset is only safe during boot (while the identity map at PML4[0] is active).

### clone_kernel_pml4 must NOT copy boot identity-map entries
- Boot code sets PML4[0] (entry 0, user-space range) to point to an identity-map PDPT at phys 0x2000.
- `clone_kernel_pml4()` previously memcpy'd all 512 PML4 entries, leaking this kernel-owned page-table page into every user PML4.
- When a user task exits, `free_user_pages()` walks PML4 entries 0-255 and asserts `PMM::is_user_page()` on every page-table page it finds — the leaked PDPT page at phys 0x2000 fails because it was allocated by the bootloader, not by `PMM::alloc_user_page()`.
- Fix: only copy kernel-space entries (256-511); zero entries 0-255.
- If a future developer changes `clone_kernel_pml4()`, they must never copy user-range PML4 entries from the kernel PML4 — the kernel may have transient mappings there (identity map, MMIO, etc.) that are kernel-owned pages.
