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

### Always use higher-half mapping for physical→virtual conversion
- Kernel code must use `0xFFFF800000000000 + phys` for physical-to-virtual conversion, regardless of what PML4 is active.
- The identity-map at PML4[0] exists in the kernel PML4 (boot setup) but is **absent** in all user PML4s because `clone_kernel_pml4()` zeroes entries 0-255.
- Relying on the identity map is fragile — it is a boot artifact and should not be assumed for kernel code that runs across context switches.
- Affected components: PMM bitmap, VMM page table walks (`get_table`, `map_page_in_pml4`, `free_user_pages`, `clone_kernel_pml4`), ELF loader segment copies.

### Fork uses shared page tables (no deep-copy)
- `clone()` calls `clone_kernel_pml4()` which copies **all 512** PML4 entries, including user entries 0-255. The child inherits the parent's code (.text), data (.data), BSS, and heap mappings via shared page-table pages.
- A **new user stack** is allocated for the child and mapped at `0x70000000` in the cloned PML4.
- The child can immediately execute the same program code as the parent (same page tables, no deep-copy).
- Consequences of sharing:
  - **Cannot call `free_user_pages()`** for a fork child that still shares page tables — would corrupt parent's address space.
  - `free_user_pages()` must tolerate kernel-owned pages (boot identity-map) that leak into user PML4s.
  - Proper isolation would require **deep-copy** (recursively copy the user page table hierarchy in `clone()`), which is a future optimization.
- **Currently chosen**: Share (copy all entries), with the leak risk. Acceptable because child always execs or exits quickly.

### free_user_pages must NOT be called on shared page tables
- Fork shares PDPT/PD/PT pages between parent and child (PML4 entries 0-255 point to the same physical page tables). Calling `free_user_pages()` on a shared PML4 would free the shared page table hierarchy, corrupting the other task's address space.
- Fix: `TaskControlBlock` has a `page_table_shared_` flag, set to `true` by `clone()`. Both `cleanup()` and `exec_into_current()` check this flag and skip `free_user_pages()` when set. The PML4 page itself is still freed.
- Consequence: per-fork allocations (user stack, PT entries for the new stack) under the shared hierarchy are leaked when the child exits. Acceptable because children always exec or exit quickly.

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

### HHDM circular dependency — page tables need HHDM to be zeroed, HHDM needs page tables
- When mapping a framebuffer at physical 0x80000000 (2 GiB) via HHDM (0xFFFF800080000000), the VMM must create page tables for the HHDM mapping.
- The boot HHDM only covers 0–128 MiB (PML4[256] → PDPT_HIGHER[0] with 64 × 2 MiB huge pages). Page table pages allocated above 128 MiB have no HHDM mapping yet.
- `VMM::get_table()` returned `HHDM_OFFSET + new_page` for the new page table page, but that virtual address was unmapped! Writing the PTE via that address caused a GPF.
- Temporary mapping via PML4[511] failed because the virtual address for PML4[511] mappings (0xFFFFFC0000000000+) was not set up — the code accessed via identity mapping instead.
- **Fix:** Reserve a 16 MiB pool in the first 128 MiB for page table pages (`PMM::alloc_page_table()`). These pages are guaranteed covered by the boot identity map (0–128 MiB) and boot HHDM huge pages. Zero them via the identity mapping (virtual = physical), return `HHDM_OFFSET + phys` which is valid because the page is in the boot HHDM range. Removed the broken PML4[511] temporary mapping entirely.
- **Lesson:** Page table allocation must guarantee that the page table pages themselves are mappable. Either allocate them from pre-mapped memory, or recursively create the HHDM mappings for them first.

### clone_kernel_pml4 must NOT copy identity-map; fork copies user entries from parent
- `clone_kernel_pml4()` is used by `elf::load()` and `exec_into_current()` to create a *fresh* user PML4. It zeroes entries 0-255 (user range) and copies 256-511 (kernel range). This ensures no boot identity-map pages leak into user PML4s.
- Fork (`TaskControlBlock::clone()`) needs the OPPOSITE: it allocates a raw PML4, copies user entries 0-255 from the PARENT's PML4 (sharing page tables), and kernel entries 256-511 from the kernel PML4.
- **First attempted fix** (broken): zero entries 0-255 in `clone_kernel_pml4()` and also call it from fork. This broke fork — the child's PML4 had no user mappings.
- **Second attempted fix** (broken): keep copying all 512 entries in `clone_kernel_pml4()` and make `free_user_pages()` tolerant of kernel-owned pages. This broke because the identity-map pages are supervisor-only, so user code within the identity-map range (e.g., 0x4011CF) gets a protection fault (#PF err=0x5).
- **Correct fix**: separate the two use cases. `clone_kernel_pml4()` yields a fresh PML4 (zeroed user range). Fork has its own PML4-construction logic that shares the parent's user entries.
- Key insight: a single "clone kernel PML4" function cannot serve both exec (fresh) and fork (shared) semantics.
