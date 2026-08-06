# SIL 3 Audit Report — GS-base / swapgs window: deferred-switch + snapshot_restore architecture

**Audit ID:** NEX-SIL3-AUD-2026-08-06-001
**Target:** `all` class freeze at test 348 `timer_deadline_miss_detection_fires` (timing cluster, ROADMAP v0.3.9)
**Subject artifacts (verified against working tree, branch `main`):**
- `src/kernel/arch/x86_64/isr_stubs.asm` (274 L)
- `src/kernel/syscall/syscall_entry.asm` (70 L)
- `src/kernel/arch/x86_64/boot/boot.asm` (132 L)
- `src/kernel/arch/x86_64/hal/{gdt,idt}.cpp`
- `src/kernel/kernel.cpp` (handle_interrupt_c, on_tick path, switch atoms)
- `src/kernel/task/scheduler.cpp` (switch_to_task arm 1685–2042, on_tick 817–1367, scan_deadlines 2509–2548, monitor_task_entry 2550–2572, scheduler_on_context_switch 2813–2855)
- `src/kernel/test/test_isolate.cpp` (snapshot_restore 531–1276)
- `src/kernel/test/test_timing.cpp:299–342` (test 348 body)
- `src/libc/syscall.h:78–86` (int $0x80)
- `src/kernel/task/task.cpp:288–332` (kUserYieldStubVa, 0x31 0xC0 0x0F 0x05 0xEB 0xFA)
- Prior findings: `ROADMAP_done.md:631–666` (timing-cluster root cause), `docs/_archive/ipc_blocking-analysis.md:188–206` (swapgs dead-code), `docs/_archive/ipc_blocking-analysis.md:232` (348 baseline hang)

**Hazard under audit:** nested interrupt during a running `snapshot_restore` → `swapgs` at the wrong moment, `MSR_KERNEL_GS_BASE` / per-CPU pointer still referencing a freed/stale test-task stack → CPU pushes/pulls the hardware iretq frame on a wrong/unaligned stack → deterministic freeze at test 348.

**Verdict (top):** **HYPOTHESIS DISPROVEN as the trigger of the test-348 freeze.** `swapgs` is statically unreachable before test 348, and the interrupt entry/exit path is GS-independent. The freeze was root-caused to the deadline-monitor dangling-pointer + INV-4 gate-spin races (ROADMAP_done.md 2026-08-05). Three latent GS/stack hazards ARE confirmed (F-1, F-2, F-3); none fire in the 0–347 prefix.

---

## 1. Interrupt entry/exit path analysis — `isr_stubs.asm`

### 1.1 Entry gate (macros → isr_common, lines 31–121)

```
ISR_NOERR n:  push 0 ; push n ; jmp isr_common      ; [rsp+0]=vec, [rsp+8]=err, [rsp+16]=RIP
ISR_ERR   n:  push n ; jmp isr_common               ; CPU err at [rsp+8]
isr_common:
  inc [rel isr_nesting_depth]                       ; RIP-relative only
  push rax; push rdx; rdtsc; ... mov [rel irq_entry_tsc], rax   ; TSC capture
  push r15..rax (15 qwords)                          ; regs[0..14]
  mov rdi,[rsp+15*8]; mov rsi,[rsp+16*8]; mov rdx,[rsp+17*8]; mov rcx,rsp; mov r8,[rel irq_entry_tsc]
  call handle_interrupt_c
```

**Audit result:**
- G1.1 — No `swapgs`, no `[gs:…]` memory operand, no FS/GS selector load anywhere in the ISR entry, body, or exit. All globals are `[rel …]` RIP-relative. **The ISR path is architecturally independent of GS base.** A nested IRQ therefore cannot trigger `swapgs`.
- G1.2 — Stack used: the current RSP as it stands (boot stack for the harness in test mode; a task's kernel stack otherwise). No IST switch on entry (all IDT entries IST=0 except vector 8 → IST1, `gdt.cpp:90`, `idt.cpp:74–76`). The double-fault stack is the only dedicated hardware stack.

### 1.2 Deferred-switch apply (lines 123–231)

```
cli
mov rax,[rel scheduler_save_rsp_to]; test; jz .restore
cmp [rel isr_nesting_depth], 2 ; ja .restore          ; depth ≤ 2 only
mov rcx,[rel scheduler_switch_generation]
call scheduler_diag_pre_save                          ; diag only
cmp rcx,[rel scheduler_switch_generation]; jne .restore   ; gen re-check
mov rax,[rel scheduler_save_rsp_to]; test; jz .restore
mov [rax], rsp                                       ; save CURRENT rsp → save_target
mov rbx, rsp                                         ; hold old rsp for abort
mov rsp,[rel scheduler_load_rsp_from]                ; load NEXT task's saved rsp
mov qword [rel scheduler_load_rsp_from],0
mov qword [rel scheduler_save_rsp_to],0
mov rcx,[rel scheduler_load_kstack_base]; mov rdx,[rel scheduler_load_kstack_top]
cmp rsp,rcx ; jb .abort_switch
cmp rsp,rdx ; jae .abort_switch                       ; apply-side RSP-owner check
call scheduler_on_context_switch                      ; update current cache (scheduler.cpp:2813)
mov qword [rel isr_nesting_depth], 1
mov rax,[rel scheduler_load_cr3_from]; test; jnz .load_cr3
mov rax,[rel scheduler_kernel_cr3]
.load_cr3: mov cr3,rax ; mov qword [rel scheduler_load_cr3_from],0
```

**Audit result:**
- G1.3 — The apply path is pure RSP/CR3 manipulation. **No GS access.** The RSP-owner check (`scheduler_load_kstack_base/top`, published at scheduler.cpp:1975–1979) is a bounds check against the NEXT task's kernel stack only.
- G1.4 — `mov rsp,[scheduler_load_rsp_from]` is the sole "wrong-stack" entry point. If `load_rsp_from` is stale/foreign, the subsequent `iretq` (in `.restore`) pops an invalid frame. This is the documented H2 race (archive: three-layer mitigation + residual ~17% boot-time window), **not a GS/swapgs defect**. The C++ publish side additionally validates the frame (`frame_ok`, scheduler.cpp:1867–1920).

### 1.3 `.abort_switch` (lines 233–241)

```
mov qword [rel scheduler_save_rsp_to],0
mov qword [rel scheduler_load_cr3_from],0
mov qword [rel scheduler_next_task_id],-1
mov rsp,rbx
```

**Audit result:**
- G1.5 — Abort restores RSP and clears the pair atoms. **It does NOT restore TSS.RSP0.** `set_tss_rsp0(next.kernel_stack_top)` ran at arm time (scheduler.cpp:1991) only when `next.page_table_` (user task). After an abort while a ring-3 current task runs, TSS.RSP0 can point at the aborted `next`'s kernel stack top. Next ring-3→ring-0 transition pushes the iretq frame on that stale RSP0. **Latent hazard F-2b** (inactive in tests 0–347: no ring-3 current task exists in the prefix; see §4.3).

### 1.4 `.restore` (lines 243–266)

```
pop rax,rbx,rcx,rdx,rsi,rdi,rbp,r8,r9,r10,r11,r12,r13,r14,r15
add rsp,16            ; drop vec+err
dec [rel isr_nesting_depth]
iretq
```

**Audit result:**
- G1.6 — iretq pops RIP/CS/RFLAGS (ring-0 source) or additionally RSP/SS (ring-3 source), matching exactly what the hardware pushed at entry. **No GS involvement.** No conditional swapgs exists at any return site. A ring-3 frame returns to user mode with GS base = user GS base (whatever it was at interrupt time) — the kernel never touches GS, so there is no mismatched-GS return.

### 1.5 Frame alignment (SIL 3 concern: "unaligned stack")

Pre-interrupt RSP ≡ 8 (mod 16) (SysV: after return-address push). Hardware pushes 3 qwords (ring0) → RSP ≡ 0. Macro + 2 qwords → RSP ≡ 0. 15 GPR pushes → RSP ≡ 8. `call handle_interrupt_c` → RSP ≡ 0 at C entry. **Alignment is preserved in the canonical path; no #GP source.**
Alignment breaks only when `mov rsp,[scheduler_load_rsp_from]` loads a misaligned/foreign value (H2 apply). Any `movaps`/SSE in `handle_interrupt_c`/`scheduler_on_context_switch` would then #GP — a secondary H2 symptom, still not GS-related.

---

## 2. `syscall_entry.asm` — the ONLY swapgs sites

```
syscall_entry:                ; LSTAR target, ring-3 `syscall` (0F 05)
  swapgs                      ; [A] GS base := MSR_KERNEL_GS_BASE
  mov [gs:0x00], rsp          ; [B] save user rsp into per-CPU slot +0
  mov rsp, [gs:0x08]          ; [C] load kernel rsp from per-CPU slot +8
  push 0; push 0xFFFFFFFF80000000
  push r15..rax (16 qwords)
  mov rdi,rax; mov rsi,rbx; mov rdx,rcx; mov rcx,rdx; mov r8,rsi; mov r9,rdi
  call syscall_handler
  pop rbx,rcx,rdx,rsi,rdi,rbp,r8,r9,r10,r11,r12,r13,r14,r15
  add rsp,16
  mov rsp,[gs:0x00]           ; [D] restore user rsp from per-CPU slot +0
  swapgs                      ; [E] GS base := user GS base
  o64 sysret
```

**Audit result:**
- G2.1 — `swapgs` appears exactly twice in the kernel: lines 21 and 69. **No other instruction in the tree reads or writes GS** (`grep swapgs|KERNEL_GS_BASE|GS_BASE|wrgsbase` across `src/` → 0 hits besides these two).
- G2.2 — **`MSR_KERNEL_GS_BASE` (0xC0000102) and `MSR_GS_BASE` (0xC0000101) are NEVER written.** `Syscall::init()` (syscall.cpp:37–48) writes only STAR/LSTAR/FMASK. `GDT::load()` (gdt.cpp:95–106) executes `mov gs,GDT_DATA` — flat descriptor base 0 → **GS base = 0 in all contexts** (kernel and user). MSR_KERNEL_GS_BASE stays at its architectural reset value 0.
- G2.3 — Consequence: after entry-`swapgs`, **GS base = 0**. `mov [gs:0x00], rsp` = store to canonical address 0x0 → **#PF (CR2=0), taken in kernel mode (CS=0x8, RSP = user stack still, CR3 = user PML4)**. `handle_interrupt_c(14,…)` sees `from_user = false` (kernel.cpp:1318–1320) → `fatal` → `panic("CPU EXCEPTION")`. **One ring-3 `syscall` = guaranteed kernel panic, not a freeze.**
- G2.4 — Reachability: libc uses `int $0x80` exclusively (`syscall.h:82`), routed via trap gate `isr_128` (idt.cpp:69–71, `InterruptVector::SYSCALL=0x80`) → `isr_common` (GS-free). The `syscall`-instruction path is reachable ONLY through the ring-3 yield stub `{31 C0 0F 05 EB FA}` at `kUserYieldStubVa=0x40000000` (task.cpp:288–295), i.e. only if a `create_user()`/fork child is actually dispatched to ring 3.

---

## 3. `snapshot_restore` window — interrupt ingress analysis

Execution model: the harness (init/PID 1) runs the test bodies and `snapshot_restore()` **in ring 0 on the linker boot stack** (`.boot_stack`, `kernel.cpp:1569`), never on a TCB kernel stack (H2 owner-resolution, scheduler.cpp:1577–1591, 1710–1749).

### 3.1 IF state
- `snapshot_restore` opens with `arch::IrqGuard guard;` (test_isolate.cpp:534) → `cli()` (irq_guard.hpp:38). **IF=0 for the entire body.**
- Maskable vectors (PIT/APIC timer, all external IRQs, all `int $0x80`) **cannot enter the window**.
- Non-maskable only: vector 2 (NMI). NMI → `isr_2` → `isr_common` → `handle_interrupt_c(2,…)` → `arch::IDT::handle_interrupt` (no NMI handler registered). It pushes onto the harness boot stack (valid), uses no GS, applies no switch (all pair atoms are null — see 3.2), and iretq's. **NMI in the window is benign w.r.t. GS and the switch atoms.**
- Exit: `arch::sti()` (test_isolate.cpp:1275, belt-and-suspenders) + IrqGuard dtor re-enable IF.

### 3.2 Switch-atom state in the window
At snapshot_restore entry the deferred-switch pair is **explicitly cleared** (test_isolate.cpp:587–592: `load_rsp_from=0, load_cr3_from=0, next_task_id=UINT64_MAX, save_rsp_to=NULL, isr_nesting_depth=0`). `on_tick()` re-arms only inside `handle_interrupt_c` → ISR, which is IF-gated off in the window. **No switch can be published or applied during the restore.**

### 3.3 swapgs reachability in the window — PROOF OF DISPROOF
`swapgs` executes only in `syscall_entry` (§2). `syscall_entry` is entered only via LSTAR from the ring-3 `syscall` instruction. In the window the CPU is ring 0, IF=0, no user context is dispatched, and no ring-3 resume (iretq) occurs. **There is no instruction stream in the window that can execute `swapgs`. The "swapgs at the wrong moment" race is impossible.**

### 3.4 What the window actually mutates (for the record)
PMM bitmaps + free list, MemPool meta/data + pin map, scheduler task arrays/idtable/fields/ReadyQueue POD, kernel PML4 user entries, HHDM PD, `write_cr3`, other tasks' kernel stacks (current task's own stack is explicitly skipped, test_isolate.cpp:948–1005), TSS.RSP0 = idle's kernel stack top (test_isolate.cpp:1184–1187), BufferPool/daemon/VFSD/IOCD state. **None of these write MSR_GS_BASE / MSR_KERNEL_GS_BASE.** The current task is re-identified by live-RSP range scan (test_isolate.cpp:894–916, 1022–1039).

---

## 4. Hardware iretq-frame placement analysis

### 4.1 Push rules
- Interrupt in ring 0: hardware pushes RIP,CS,RFLAGS on the **current RSP**. Current task's own kernel/boot stack — never freed during the window (the harness's live stack is the boot stack, which `snapshot_restore` never overwrites).
- Interrupt in ring 3: hardware switches RSP to **TSS.RSP0**, then pushes RIP,CS,RFLAGS,RSP,SS.
- `syscall` (LSTAR): pushes **nothing**, no stack switch; `syscall_entry` must install the kernel stack itself. In this kernel that installation is `mov rsp,[gs:0x08]` — which is broken (GS base 0, §2.3). This is why `sysret`+`swapgs` user return in this kernel can never be reached without a prior panic.

### 4.2 The freeze mechanism actually documented at test 348
ROADMAP_done.md:631–666 root-causes the 348 freeze (2026-08-05, verified):
1. **Dangling deadline-monitor pointer** — `reboot_from_table()` frees the monitor TCB; `s_monitor_task_` dangles into a reused MemPool block; `on_tick` writes `state=READY`+`enqueue_ready()` into the reused block; `trigger_deadline_monitor_scan` waits forever → silent freeze.
2. **INV-4 gate-spin races** — helper lambdas `Semaphore::wait()` (set BLOCKED, return immediately) then self-terminate before the harness observes BLOCKED → harness spins `while(state != BLOCKED)` forever.
3. `timer_period_reload` leak.
Fix committed; `timing` 18/18, deadline classes green. Test 348's current body (test_timing.cpp:299–342) uses `trigger_deadline_monitor_scan() → scan_deadlines()` synchronously (test_sched_helpers.hpp:160–168) and BLOCKED-spins — the two fixed patterns.

### 4.3 Ring-3 / swapgs reachability in the 0–347 prefix
Class order (`test_registry.cpp:214–535`): … `process`, `syscall`, `arch`, `vmm`, `cross_arch`, … `sporadic`, `atomic`, `spinlock`, **`timing` (test 348)** … `process`/`syscall`/`vmm` precede `timing` in the registration list, but the audit report (`audits/test-suite-v0.3.10.md`, T0-2) documents that no test dispatches user workers to ring 3 before the deadline cluster (`atomic_sb_litmus` workers never dispatch; kernel-driven tests only). Archival evidence (ipc_blocking-analysis.md:232) states tests 1–347 PASS — a single ring-3 `syscall` would have caused a panic (§2.3), not a pass. **Therefore `swapgs` provably never executed before test 348, and the per-CPU/GS window cannot be the 348 trigger.**

---

## 5. Findings register (SIL 3)

| ID | Sev | Category | Finding | Status | Evidence |
|---|---|---|---|---|---|
| F-1 | **HIGH (latent)** | GS config | `MSR_KERNEL_GS_BASE`/`MSR_GS_BASE` never written; entry-`swapgs` → GS base 0 → `mov [gs:0],rsp` #PF phys 0, kernel-mode → panic. LSTAR path is a landmine. | Latent — reachable only if a ring-3 task executes `syscall` (yield stub 0x40000000). Inactive in tests 0–347 (no ring-3 dispatch). | syscall_entry.asm:21–23; gdt.cpp:95–106; syscall.cpp:37–48; task.cpp:288–295; kernel.cpp:1316–1320 |
| F-2 | **MED (latent)** | TSS.RSP0 | (a) RSP0 set at arm time only for user tasks (scheduler.cpp:1991); ring-0 dispatches leave it stale — harmless because ring-0 transitions never use RSP0. (b) `.abort_switch` does not restore RSP0; if the current task is ring 3, the next ring-3→ring-0 frame lands on the aborted next-task's stack top. | (a) benign by construction in ring-0-only runs. (b) inactive in tests 0–347 (no ring-3 current). | isr_stubs.asm:233–241; scheduler.cpp:1991; kernel.cpp:540 |
| F-3 | **MED (design)** | Per-CPU | No per-CPU struct exists; GS slot +0/+8 (syscall_entry:22–23, 68) is never initialized → the `[gs:…]` exchange is a NULL-write landmine (§2.3), not a stale-task-stack deref. The user's "per-CPU points to freed test-task stack" scenario requires a written KERNEL_GS_BASE; none exists. | Latent. | syscall_entry.asm; grep across src |
| F-4 | **INFO** | Freeze at 348 | Not GS/swapgs. Root-caused 2026-08-05: dangling deadline-monitor pointer + INV-4 gate-spin; fixed (`timing` 18/18). Any current 348 freeze must be re-attributed (H2 residual at 77/78 precedes it in `all`). | Documented/closed. | ROADMAP_done.md:631–666 |
| F-5 | **INFO** | Nested-IRQ-in-window | IrqGuard `cli()` (IF=0) + pair-atoms cleared ⇒ only NMI can enter the window; NMI path is GS-free and cannot apply a switch. "Nested interrupt during snapshot_restore triggers swapgs" is impossible. | Closed — disproven. | test_isolate.cpp:534,587–592; irq_guard.hpp:38 |
| F-6 | **INFO** | Frame alignment | Canonical ISR path keeps RSP ≡ 0 mod 16 at the `call` boundary; no alignment fault. Misalignment only via stale `load_rsp_from` (H2), which is RSP-, not GS-, based. | Closed. | isr_stubs.asm:86–121 |

---

## 6. Corrective actions (recommended, in priority order)

1. **Disable the LSTAR landmine or initialize GS:** either remove the dead `syscall_entry` (route everything through `int $0x80`/`isr_128`, which is GS-free and already the sole live path), or allocate a per-CPU slot and write `MSR_GS_BASE`/`MSR_KERNEL_GS_BASE` at `Syscall::init()`, and make `kUserYieldStub` use `int $0x80` (vector 128 trap gate, ring-3 accessible) instead of `0F 05` — this also removes the audit's T0-6 VA collision vector at 0x40000000.
2. **Abort-path RSP0 fix:** in `.abort_switch` (or the arm side), pair `set_tss_rsp0` with a revert to the current task's kernel-stack top when the switch is refused; only relevant once ring-3 dispatch is live.
3. **Keep the pair-atoms clear + IF=0 invariant** around `snapshot_restore` as-is (already correct); add a `JARVIS_ASSERT(interrupts_disabled())` debug check at test_isolate.cpp:536.
4. **Deadline-monitor hard guarantees:** the fix in §4.2 must hold `s_monitor_task_` valid across `reboot_from_table()` (cleanup() clears it) and keep `trigger_deadline_monitor_scan` on the synchronous `scan_deadlines()` path until the monitor wake is proven reliable under snapshot isolation.

**Auditor note:** no test-code or kernel-code change was made during this audit; findings are static-analysis evidence against the working tree (branch `main`).
