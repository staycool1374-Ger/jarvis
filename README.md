<p align="center">
  <img src="nexios-rtos-logo.png" alt="NexIOS RTOS Logo" width="600"/>
</p>

<h1 align="center">NexIOS RTOS</h1>
<p align="center">
  <em>A deterministic, safety-critical real-time operating system built from scratch in freestanding C++20.</em>
</p>
<p align="center">
  <strong>🌐 Project website: <a href="https://nexios-2.jimdosite.com">https://nexios-2.jimdosite.com</a></strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C++20-freestanding-00599C?style=flat-square&logo=cplusplus" alt="C++20 Freestanding"/>
  <img src="https://img.shields.io/badge/arch-x86__64-1f425f?style=flat-square" alt="x86_64"/>
  <img src="https://img.shields.io/badge/scheduling-hard%20real--time-critical?style=flat-square&logo=clockifier" alt="Hard Real-Time"/>
  <img src="https://img.shields.io/badge/concurrency-RAII%20guarded-2ea44f?style=flat-square" alt="RAII Concurrency"/>
  <img src="https://img.shields.io/badge/tests-881%20passing-2ea44f?style=flat-square" alt="881 Tests Passing"/>
  <img src="https://img.shields.io/badge/license-GPLv3-blue?style=flat-square" alt="GNU General Public License v3"/>
</p>

---

## Overview

NexIOS RTOS is an independent, ground-up implementation of a real-time operating system. It is built in **freestanding C++20** — no libc, no libstdc++, no runtime.

The kernel is currently monolithic, serving userspace processes at Ring 3 via a `int 0x82` syscall gate (47 syscalls). The architecture is mid-transition toward a **capability-based microkernel**, where drivers, VFS, and block I/O are externalised to sandboxed Ring 3 servers communicating through IPC capabilities.

Current version: **v0.3.6** — Scheduler/IPC/Sync, O(1) scheduler hardening, MemPool bitmap fixes.

---

## Why NexIOS

NexIOS is engineered so that a elf file runs as a dedicated user-task, scheduled deterministically, isolated in its own address space, and sandboxed.

---

## Recent Development

Here is what the roadmap and the commit history shows:

1. **A true from-scratch freestanding C++20 kernel** — no libc, no libstdc++, no runtime. Every scheduler data structure, VFS vnode operation, and page-table walk is pure freestanding C++ with `constexpr`-enforced layout constants.
2. **An O(1) priority-bitmap scheduler** with rate-monotonic dispatch, priority inheritance, and a priority ceiling protocol.
3. **O(1) everywhere on the hot path.** The ready queue, the priority buckets, the id-table probes, and free-list allocation.
4. **Zero-alloc by design.** IPC `MessageQueue`/`Notify`/`EventGroup` are embedded in the TCB; a pre-allocated `BufferPool` and a deterministic slab `MemPool` replace every `new`/`delete` in the kernel.
5. **Fork with eager page-table deep-copy** and a private kernel-stack window with guard pages.
6. **Per-priority kernel-stack sizing** with guard pages and #PF detection..
7. **Snapshot-based test isolation** powering an **881-test suite** that rewinds the entire kernel (PMM, MemPool, page-table pool, ready queue) between tests.
8. **A hardened user/kernel trust boundary**: every Ring-3 pointer crossing the `int 0x82` gate is validated (`CheckedPtr`).
9. 
**The goal is simple:** NexIOS is being built so that any program — your program — can run as the dedicated user-task it was designed for, under a scheduler and memory model.

---

## Architectural Pillars

### Modern Freestanding C++20

The entire kernel — every scheduler data structure, every VFS vnode operation, every page-table walk — is written in freestanding C++20. There is no C legacy layer, no assembly veneer beyond the boot entry point and ISR stubs, and no reliance on hosted runtime primitives.

- **C++20 Concepts** used extensively for compile-time validation of synchronisation primitive interfaces, memory allocator traits, and architecture HAL requirements.
- **constexpr** and **consteval** enforce that all scheduling parameters, memory layout constants, and system limits are known at compile time.
- **RAII** is not a userspace luxury — it is the fundamental lifecycle model for every kernel resource: interrupt guards, page-table mappings, heap allocations, IPC mailbox handles, and file descriptors.
- **noexcept** by default. The kernel is compiled with `-fno-exceptions`, so every function contract is enforced statically or via assertion.
- No `malloc`, no `printf`, no `errno`. All memory comes from a deterministic slab allocator (`MemPool`), and all diagnostics go through a custom binary `Logger`.

### RAII-First Kernel Synchronisation

Every critical section in the kernel is enforced by a scoped guard, not by convention. The primary mechanism is `IrqGuard`:

```cpp
// kernel/arch/irq_guard.hpp
class [[nodiscard]] IrqGuard {
public:
    IrqGuard() noexcept { if (interrupts_enabled()) { cli(); irq_was_ = true; } }
    ~IrqGuard() noexcept { if (irq_was_) sti(); }

    IrqGuard(const IrqGuard&)            = delete;
    IrqGuard& operator=(const IrqGuard&) = delete;
    IrqGuard(IrqGuard&&)                 = delete;
    IrqGuard& operator=(IrqGuard&&)      = delete;

private:
    bool irq_was_;
};
```

This eliminates an entire class of check-then-act races because the guard cannot be forgotten, duplicated, or left dangling. The pattern extends to mutexes (with priority inheritance), semaphores, event groups, and the scheduler's internal dispatch — all protected by RAII wrappers that enforce unlock-on-destruction.

### Microkernel Paradigm Shift (In Progress)

NexIOS is intentionally transitioning from a monolithic service layer to a capability-based microkernel: a deliberate architectural migration planned over Phases 3–8 of the roadmap.

- **Phase 7 (v0.7.x):** VFS (`vfsd`) and block I/O (`iocd`) are externalised to Ring 3 servers. Filesystem drivers (FAT32, tmpfs) run as isolated userspace processes behind an IPC gateway. No kernel code holds a mount table reference.
- **Phase 8 (v0.8.x):** The kernel is reduced to scheduler + IPC + page-table manager + interrupt routing. The Shell, init (PID 1), VFS, and all device drivers run as Ring 3 capability-bearing servers. `SYS_CAP_GRANT` / `SYS_CAP_REVOKE` gate every cross-server access — no server can touch another server's MMIO region or memory without explicit capability delegation.

This migration is architected from the start: the current monolithic ring-0 service layer is structured as a set of isolated subsystems with clean interface boundaries, making the extraction to individual servers a matter of wrapping, not rewriting.

---

## Features

For a complete catalog of all implemented features — scheduler, IPC, VFS, hardware enablement, shell, test framework, dmesg diagnostics, and architectural comparison — see [`README_done.md`](README_done.md).

---

## IrqGuard in Practice

The following is extracted from the scheduler's `reschedule()` path — a critical section that must select the next task and prepare the context switch globals without interruption. The `IrqGuard` ensures `cli()` on entry and `sti()` (if interrupts were on) on any exit path, including early returns:

```cpp
void Scheduler::reschedule() noexcept {
    arch::IrqGuard guard;                     // RAII: cli() on construction
    if (task_count_ <= 1) return;             // safe early return — guard handles sti()

    auto* current = tasks_[current_index_];
    if (!current) return;

    auto* next = next_task();
    if (next && next != current) {
        if (next->state != TaskState::READY &&
            next->state != TaskState::RUNNING) return;

        switch_to_task(current, next);        // sets scheduler globals for ISR epilogue
    }
}                                             // guard destructor: sti()
```

Every synchronisation primitive in the kernel — `Mutex`, `Semaphore`, `EventGroup`, `Notify`, `MessageQueue` — follows the same RAII contract, making it structurally impossible to leak a critical section.

---

## Roadmap

Completed phases (v0.2.0–v0.2.23) archived in [`README_done.md`](README_done.md).

- [ ] **Phase 7 — Hard Real-Time** — O(1) bitmap scheduler, HPET, WCRT analysis, priority ceiling protocol, idle-task RAM March-C
- [ ] **Phase 8 — SMP & Multicore** — APIC, per-CPU run queues, cache-colouring allocator, TLB shootdown
- [ ] **Phase 9 — System Integration** — 24h stress test, safety hardening, deterministic userspace libc
- [ ] **Phase 10 — Safety Systems** — Hardware/software watchdog, wait-for-graph deadlock detection
- [ ] **Phase 11 — Microkernel Transition** — Externalise VFS, drivers, block I/O to Ring 3 capability servers

Full roadmap at [`ROADMAP.md`](ROADMAP.md).

---

## Build & Quick Start

### Prerequisites

```bash
sudo apt install build-essential git wget xorriso dosfstools \
    x86_64-linux-gnu-gcc binutils qemu-system-x86
```

### Build & Run

```bash
git clone <repo-url>
cd os
make debug          # Debug build with 881-test suite
make qemu-iso       # Launch in QEMU with serial console
make release        # Optimised release build (no tests)

# Testing targets (QEMU)
make execute-test x86 debug selftest  # Safe class (CI gate)
make execute-test x86 debug all-1    # First half
make execute-test x86 debug all-2    # Second half
make execute-test x86 debug <class>  # Specific test class

# Renode simulation (multi-arch)
make run-renode RENODE_ARCH=x86_64   # x86_64 via SeaBIOS+ISO
make renode-test          # Renode CI validation
```

### Build Architecture

```
  [ Userspace Apps ] <─── Ring 3 Isolation
────── [ Syscall Interface: int 0x82 (47 syscalls) ] ──────
  [ Shell (Kernel Task, 36 built-ins) ]  [ RMS Scheduler        ]
  [ VFS / Initrd / Devfs / Procfs / FAT32 ] [ Priority IPC Mailbox]
  [ Virtual Memory (VMM, 4-level PT)    ]  [ Notify & Event Groups]
  [ O(1) PID→TCB Hash Table             ]  [ Priority Inheritance ]
  [ Physical Memory (PMM, Buddy Alloc)   ]  [ Slab Alloc (MemPool) ]
  [ Hardware: Serial, KBD, Framebuffer,   ]  [ ATA PIO, PIT, RTC    ]
  [ PCI, Virtio, ACPI                    ]  [ RNG, FPU Lazy Switch ]
  [ Gcov, Driver Registry, Integrity     ]  [ Deadlock Detection   ]
═════════════ Monolithic Kernel (Ring 0) ═════════════
```

---

## Call for Contributions

NexIOS RTOS is an architectural project first and a feature project second. We are seeking contributions from engineers who like to participate.
If this aligns with your engineering philosophy, open an issue or pull request.

---

## License

NexIOS RTOS is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License**, either version 3 of the License, or (at your option) any later version. See [`LICENSE.txt`](LICENSE.txt) for the full text.
