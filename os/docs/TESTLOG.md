# Jarvis RTOS — Testlog

## v0.0.6 — Baseline

**Datum**: 2026-06-05  
**Build**: `-O3`, QEMU+KVM i7-3930K  
**Status**: ✅ ALLE TESTS BESTANDEN

| Test | Ergebnis |
|------|----------|
| string.memset | PASS |
| string.memset_small | PASS |
| string.memcpy | PASS |
| string.memcpy_small | PASS |
| string.memcmp_equal | PASS |
| string.memcmp_diff | PASS |
| string.strlen | PASS |
| utils.align_up | PASS |
| utils.align_down | PASS |
| error_or.value | PASS |
| error_or.error | PASS |
| error_or.void_ok | PASS |
| error_or.void_error | PASS |
| pmm.alloc_free | PASS |
| pmm.alloc_contiguous | PASS |
| pmm.alloc_zero_size | PASS |
| mempool.alloc_small | PASS |
| mempool.alloc_large | PASS |
| mempool.alloc_too_large | PASS |
| mempool.reuse | PASS |
| ipc.create_mailbox | PASS |
| ipc.send_receive | PASS |
| ipc.mailbox_full | PASS |
| **Total** | **23 PASS, 0 FAIL, 0 SKIP** |

---

## v0.0.7 — Userspace-Infrastruktur

**Datum**: 2026-06-05  
**Build**: `-O3`, QEMU+KVM i7-3930K  
**Änderungen**:
- `handle_interrupt_c` 4. Parameter `uint64_t* regs` für Registerzugriff
- Vector `0x80` (int $0x80) wird in `handle_interrupt_c` abgefangen → `syscall_handler()`
- Bei EXIT-Syscall (TERMINATED) wird `Scheduler::reschedule()` für sofortigen Kontextwechsel aufgerufen
- EXIT-Handler: `while(true) hlt` entfernt (Bug), setzt nur `TaskState::TERMINATED`
- `Scheduler::reschedule()` öffentlich; `rate_monotonic_schedule()` behandelt TERMINATED-Tasks korrekt
- GDT-Bugfix: `ltr` nur einmal in `load()`, nicht mehr in `set_tss_rsp0()` → verhinderte wiederholtes LTR
- `create_user`-Bugfix: `kernel_stack_top` als höhere-Halb-Virtadresse statt physikalisch → TSS.RSP0 nun gültig in User-Page-Table
- User-Task wird via Maschinencode-Stub (0x100000) + `int $0x80`+ EXIT ausgeführt
- Test: `g_user_task_ran`-Flag via Syscall #99 gesetzt

**Status**: ✅ ALLE TESTS BESTANDEN

| Test | Ergebnis |
|------|----------|
| string.memset | PASS |
| string.memset_small | PASS |
| string.memcpy | PASS |
| string.memcpy_small | PASS |
| string.memcmp_equal | PASS |
| string.memcmp_diff | PASS |
| string.strlen | PASS |
| utils.align_up | PASS |
| utils.align_down | PASS |
| error_or.value | PASS |
| error_or.error | PASS |
| error_or.void_ok | PASS |
| error_or.void_error | PASS |
| pmm.alloc_free | PASS |
| pmm.alloc_contiguous | PASS |
| pmm.alloc_zero_size | PASS |
| mempool.alloc_small | PASS |
| mempool.alloc_large | PASS |
| mempool.alloc_too_large | PASS |
| mempool.reuse | PASS |
| ipc.create_mailbox | PASS |
| ipc.send_receive | PASS |
| ipc.mailbox_full | PASS |
| vmm.clone_kernel_pml4 | PASS |
| task.create_user.alloc | PASS |
| task.create_user.pagetable | PASS |
| task.create_user.stack | PASS |
| task.create_user.frame | PASS |
| syscall.int80_dispatch | PASS |
| task.user_execution | PASS |
| **Total** | **30 PASS, 0 FAIL, 0 SKIP** |

## v0.0.8 — ELF-Loader + initrd + Dateisyscalls

**Datum**: 2026-06-05  
**Build**: `-O3`, QEMU+KVM i7-3930K  
**Änderungen**:
- ELF64-Loader: `validate_header()` + `load()` (segments, user stack, iretq frame, eigene Page-Table)
- initrd: cpio newc-Parser, `find(path)` → `{data, size}`, via `objcopy -I binary` in Kernel gelinkt
- Dateisyscalls: OPEN(9), READ(10), CLOSE(11), FSTAT(12), WRITE(13)
- Userspace-Test-ELF `main.S.elf` (mov $99, %rax; int $0x80; EXIT)
- Bugfix `ELF_MAGIC`: Byte-Reihenfolge korrigiert (`0x7F454C46` → `0x464C457F` für LE-Lesen)

**Status**: ✅ ALLE TESTS BESTANDEN

| Test | Ergebnis |
|------|----------|
| string.memset | PASS |
| string.memset_small | PASS |
| string.memcpy | PASS |
| string.memcpy_small | PASS |
| string.memcmp_equal | PASS |
| string.memcmp_diff | PASS |
| string.strlen | PASS |
| utils.align_up | PASS |
| utils.align_down | PASS |
| error_or.value | PASS |
| error_or.error | PASS |
| error_or.void_ok | PASS |
| error_or.void_error | PASS |
| pmm.alloc_free | PASS |
| pmm.alloc_contiguous | PASS |
| pmm.alloc_zero_size | PASS |
| mempool.alloc_small | PASS |
| mempool.alloc_large | PASS |
| mempool.alloc_too_large | PASS |
| mempool.reuse | PASS |
| ipc.create_mailbox | PASS |
| ipc.send_receive | PASS |
| ipc.mailbox_full | PASS |
| vmm.clone_kernel_pml4 | PASS |
| task.create_user.alloc | PASS |
| task.create_user.pagetable | PASS |
| task.create_user.stack | PASS |
| task.create_user.frame | PASS |
| syscall.int80_dispatch | PASS |
| task.user_execution | PASS |
| elf.validate_header | PASS |
| elf.validate_header_invalid | PASS |
| initrd.find_hello | PASS |
| initrd.find_nonexistent | PASS |
| syscall.open_read_close | PASS |
| elf.load_from_initrd | PASS |
| **Total** | **36 PASS, 0 FAIL, 0 SKIP** |

## v0.0.9 + v0.0.10 (siehe Endabschnitt)

## v0.1.0 — Userspace-Shell + Programme (AKTUELL)

**Datum**: 2026-06-05
**Build**: `-O3`, QEMU, x86-64
**Änderungen**:
- **SYS_EXEC (20)**: Lädt ELF via VFS-Pfad, ersetzt aktuellen Task (exec_into_current)
- **VMM::free_user_pages()**: Räumt alte User-Page-Tables beim exec auf
- **initrd::readdir()**: Iteration über cpio-Archiv-Einträge
- **initrd_root_readdir**: Directory-Listing auf `/` (VFS-Ebene)
- **ELF-Loader argv/envp**: Korrekter SysV-ABI-Stack-Frame (argc, argv[], envp[], Strings)
- **Kernel-Shell**: `cd` (CHDIR-Wrapper), `export` (Platzhalter), `runelf <path.elf>` (lädt ELF als neuen Task)
- **Userspace-Programme**: ls.c, cat.c, top.c (via initrd ladbar, via `runelf` startbar)

**Status**: ✅ ALLE TESTS BESTANDEN

| Test | Ergebnis |
|------|----------|
| initrd.find_ls | PASS |
| initrd.find_cat | PASS |
| initrd.find_top | PASS |
| initrd.readdir | PASS |
| vfs.readdir_initrd | PASS |

## Gesamtteststand v0.1.0

| Metrik | Wert |
|--------|------|
| Tests gesamt | 63 |
| PASS | 63 |
| FAIL | 0 |
| SKIP | 0 |
| Status | ✅ ALLE TESTS BESTANDEN |

## v0.1.1 — VFS-Grundlage (Vnode + per-Task-FDs)

**Datum**: 2026-06-05
**Build**: `-O3`, QEMU+KVM i7-3930K
**Änderungen**:
- `Vnode`/`VnodeOps`-Typen (Funktionspointer-Tabelle, kein RTTI)
- Per-Task `FdTable` in `TaskControlBlock` (32 FDs, embedded)
- `FileDescription`-Struktur mit Vnode-Zeiger + offset + flags
- `clone_fdtable()` bei `create_user()` — fd-Isolation zwischen Tasks
- `cwd` + `cwd_vnode` in `TaskControlBlock`
- Syscall-Implementierung auf Vnode-API umgestellt
- `strcmp`/`strncmp` zu string.hpp hinzugefügt
- LSEEK(14), IOCTL(15), READDIR(16), STAT(17), DUP(18), CHDIR(19) Syscalls

**Status**: ✅ ALLE TESTS BESTANDEN

| Test | Ergebnis |
|------|----------|
| vfs.fd_alloc | PASS |
| vfs.fd_alloc_free | PASS |
| vfs.cwd_default | PASS |
| vfs.cwd_vnode_set | PASS |
| vfs.resolve_root | PASS |

## v0.1.2 — Devfs + Mount-System

**Datum**: 2026-06-05
**Build**: `-O3`, QEMU+KVM i7-3930K
**Änderungen**:
- Mount-Table (fixed array, longest-prefix-match)
- Pfadauflösung `resolve(path)` — absolut/relativ, `/`, `..`
- Devfs: `/dev/tty` (Terminal Ausgabe + Tastatur Eingabe, O_NONBLOCK)
- Devfs: `/dev/null` (data sink)
- Devfs: `/dev/console` (serial + framebuffer)
- Devfs: `/dev/kbd` (raw scancodes)
- Blocking-Read auf `/dev/tty` via Scheduler `BLOCKED`-State + tty_wake_readers()
- initrd als VFS-Filesystem gemountet (`/`)
- Keyboard-IRQ weckt tty-Leser via `tty_wake_readers()`

| Test | Ergebnis |
|------|----------|
| vfs.dev_tty_open | PASS |
| vfs.dev_null_write | PASS |
| vfs.dev_console_write | PASS |
| vfs.resolve_hello | PASS |
| vfs.open_hello_syscall | PASS |
| vfs.read_hello_syscall | PASS |

## v0.1.3 — Procfs + Syscalls

**Datum**: 2026-06-05
**Build**: `-O3`, QEMU+KVM i7-3930K
**Änderungen**:
- Procfs: `/proc/meminfo` (dynamisch generiert aus PMM-Werten)
- Procfs: `/proc/self` (Symlink auf aktuellen Task)
- Procfs: `/proc/[pid]/stat` (Task-Statistiken)
- READDIR-Syscall (16) für Directory-Enumeration
- STAT-Syscall (17) für pfadbasierte Stat-Abfragen
- DUP-Syscall (18) für fd-Redirection

| Test | Ergebnis |
|------|----------|
| vfs.proc_meminfo_read | PASS |
| vfs.lseek_set | PASS |
| vfs.dup | PASS |
| vfs.stat_path | PASS |
| vfs.readdir_dev | PASS |

## v0.1.4 — Blocking-IPC + Service-Vorbereitung

**Datum**: 2026-06-05
**Build**: `-O3`, QEMU+KVM i7-3930K
**Änderungen**:
- `Mailbox::waiting_sender` / `waiting_receiver` für Blocking-IPC
- `send()` weckt wartenden Receiver (state → READY)
- `receive()` weckt wartenden Sender
- `send_sync()` blockiert bei vollem MB (state → BLOCKED, reschedule)
- `destroy_mailbox()` weckt alle Wartenden

| Test | Ergebnis |
|------|----------|
| ipc.blocking_create | PASS |
| ipc.blocking_send_receive | PASS |

## v0.0.9a — Syscall-Rückgabewert + Blocking-IPC-Fix (Zwischenversion)

**Datum**: 2026-06-05
**Build**: `-O3`, QEMU+KVM i7-3930K
**Änderungen**:
- **Syscall-Rückgabewert-Propagation**: `handle_interrupt_c` schreibt Resultat in `regs[0]` (rax auf Stack) → userspace sieht Returnwert
- **Scheduler-Bugfix**: `rate_monotonic_schedule()` überschreibt BLOCKED-Zustand nicht mehr mit READY (prüft `== RUNNING`)
- **SYS SEND (1) Fix**: `sender_id` wird aus `current_task->id` gesetzt (nicht mehr aus arg0)
- **SYS RECEIVE (2)**: Blockierend — wartet auf Mailbox wenn leer (waiting_receiver, BLOCKED, reschedule)
- **SYS SEND_SYNC (3)**: Neu implementiert — sendet und blockiert bis Reply, kopiert Reply-Daten in Userspace-Buffer

| Test | Ergebnis |
|------|----------|
| syscall.ipc_send_receive | PASS |
| syscall.ipc_send_sync | PASS |
| syscall.return_value | PASS |

## v0.0.10 — libc-Portierung (AKTUELL)

**Datum**: 2026-06-05
**Build**: `-O3`, QEMU+KVM i7-3930K
**Änderungen**:
- **libc**: src/libc/ mit crt0.S + C-Headern + Implementationen
- **syscall.h**: Inline-Assembler-Wrapper für `int $0x80` (rax=num, rbx=arg0, rcx=arg1, rdx=arg2, rsi=arg3)
- **crt0.S**: Userspace-Entry-Point mit ABI-konformem Stack-Setup (argc/argv/environ → main → exit)
- **Standard-Header**: unistd.h, stdlib.h, stdio.h, string.h, ctype.h, stdarg.h, errno.h, sys/types.h, sys/stat.h
- **Makefile**: libc.a-Build + automatische Linkage von Userspace-C-Programmen mit crt0 + libc
- **ELF-Loader**: Heap-Region (0x60000000, 1 MiB) gemappt + argv-Stack-Frame (argc=0) + stdin/stdout/stderr auf /dev/tty vorbelegt
- **Framebuffer-Bugfix**: FB wird zusätzlich im Higher-Half (0xFFFF8000…) gemappt → auch aus User-Page-Tables zugänglich → kein #PF mehr bei Terminal::scroll() während Syscalls
- **Userspace-C-Programm**: hello.c (open /dev/tty, write, close, exit) via initrd ladbar

| Test | Ergebnis |
|------|----------|
| (keine neuen Tests — ELF-Loading wird durch `elf.load_from_initrd` abgedeckt) | PASS |

## Gesamtteststand v0.0.10

| Metrik | Wert |
|--------|------|
| Tests gesamt | 58 |
| PASS | 58 |
| FAIL | 0 |
| SKIP | 0 |
| Status | ✅ ALLE TESTS BESTANDEN |

## Gesamtteststand v0.1.0

| Metrik | Wert |
|--------|------|
| Tests gesamt | 63 |
| PASS | 63 |
| FAIL | 0 |
| SKIP | 0 |
| Status | ✅ ALLE TESTS BESTANDEN |
