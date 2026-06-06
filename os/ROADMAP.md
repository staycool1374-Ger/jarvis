# Jarvis RTOS Roadmap

## Version 0.0.1 (Basis)
- [x] x86_64 Long Mode Boot
- [x] Framebuffer Terminal
- [x] PS/2 Tastatur
- [x] Shell mit Grundbefehlen
- [x] PMM (Physical Memory Manager)
- [x] VMM (Virtual Memory Manager)
- [x] MemPool (Slab-Allocator)
- [x] Task/Scheduler (Rate Monotonic)
- [x] IPC (Inter Process Communication)
- [x] Syscall-Grundgerüst
- [x] Demo-Programm (Mandelbrot)

## Version 0.0.2 — Clean Code & Security
- [x] Single-User Optimierung
- [x] Security-Review: Null-Checks, Buffer-Overflow-Prävention
- [x] Performance-Review: Inlining, constexpr, Overhead-Reduktion
- [x] Clean Code: Namenskonventionen, const-correctness, noexcept

## Version 0.0.3 — Doxygen + Exception Handling + Dynamic Memory
- [x] Doxygen-Konfiguration (Doxyfile)
- [x] Header-Dokumentation für alle Module
- [x] Error-Code-basiertes Exception-Handling (ErrorOr\<T\>)
- [x] Heap-Allocator mit PMM-Fallback
- [x] OOM-Handling

## Version 0.0.4 — Driver Layer
- [x] Driver-Registry (Treiber-Registrierung)
- [x] modprobe/modlist Shell-Befehle
- [x] Standard-Hardware-Treiber als Module
- [x] Driver-Initialisierung beim Boot

## Version 0.0.5 — Test Methodology + Benchmark
- [x] Benchmark-Shell-Befehl (cpu, alloc)
- [x] Kernel-interne Self-Test-Registry
- [x] Self-Tests für PMM, String, Utils, ErrorOr, IPC, MemPool
- [x] selftest Shell-Befehl (alle Tests oder nach Name)
- [x] Fix: Blank screen bug (triple fault im Task-Kontextwechsel)

## Version 0.0.6 — Finale Optimierung & Stabilität
- [x] Build auf -O3 umgestellt
- [x] Keine Compiler-Warnungen unter -O3
- [x] Keine Compiler-Fehler unter -O3
- [x] Mandelbrot-CPU-Benchmark gegen Linux (RTOS: 35 ms, Linux: 29 ms = 83%)
- [x] Vollständiger Clean-Code-Review
- [x] Letzte Optimierungsrunde
- [x] Ausstehende Bugfixes

## Version 0.0.7 — Userspace-Infrastruktur
- [x] `handle_interrupt_c` 4. Parameter `uint64_t* regs` für Registerzugriff
- [x] Vector `0x80` (int $0x80) in `handle_interrupt_c` abgefangen → `syscall_handler()`
- [x] Unit-Test: `syscall.int80_dispatch` (C-level dispatch)
- [x] Integrationstest: `task.user_execution` (User-Task via Scheduler + Ring 3)
- [x] GDT-Bugfix: `ltr` nur einmal in `load()`
- [x] `create_user`-Bugfix: `kernel_stack_top` als höhere-Halb-Virtadresse
- [x] EXIT-Syscall: `while(true) hlt` entfernt, `reschedule()` nach TERMINATED
- [x] `Scheduler::reschedule()` für sofortigen Kontextwechsel bei EXIT
- [x] 30/30 Tests PASS

## Version 0.0.8 — ELF-Loader + initrd + Dateisyscalls
- [x] ELF64-Loader (validate_header, load) für statisch gelinkte ELF-Binaries
- [x] initrd (cpio newc-Archiv) via objcopy in Kernel gelinkt
- [x] Dateisyscalls: OPEN, READ, CLOSE, FSTAT, WRITE
- [x] Userspace-Test-ELF (main.S.elf, syscall #99 + EXIT) via initrd ladbar
- [x] Bugfix: ELF_MAGIC Byte-Reihenfolge für LE-Lesen korrigiert
- [x] 36/36 Tests PASS

## Version 0.0.9 — Syscall-Rückgabewert + Blocking-IPC
- [x] Syscall-Rückgabewert-Propagation in handle_interrupt_c (regs[0] = result)
- [x] Scheduler-Bugfix: BLOCKED-Zustand wird nicht mehr überschrieben
- [x] SYS SEND: sender_id aus current_task->id
- [x] SYS RECEIVE: blockiert bei leerer Mailbox
- [x] SYS SEND_SYNC: send + block + reply
- [x] 57 Tests PASS

## Version 0.0.10 — libc-Portierung (AKTUELL)
- [x] Minimal-libc für Userspace (syscall-Wrapper, crt0, printf, string.h, etc.)
- [x] Standard-Header (stdlib.h, stdio.h, string.h, unistd.h, sys/stat.h, ...)
- [x] C-Userspace-Programm (hello.c) via initrd ladbar + ausführbar
- [x] Heap-Region + argv-Stack in ELF-Loader
- [x] stdin/stdout/stderr auf /dev/tty vorbelegt
- [x] Framebuffer Higher-Half-Mapping (fix für #PF bei Terminal::scroll)
- [x] 57 Tests PASS

## Version 0.1.0 — Userspace-Shell + Programme
- [x] SYS_EXEC syscall (20) — replace current task with ELF from VFS path
- [x] ELF Loader: exec_into_current() — reload user-space without new TCB
- [x] ELF Loader: proper argv/envp stack setup (SysV ABI)
- [x] ls.c, cat.c, top.c — userspace C programs via initrd
- [x] cd, export, runelf — kernel shell built-ins
- [x] initrd_root_readdir — directory listing on /
- [x] VMM::free_user_pages() — clean up old page tables on exec
- [x] 63 Tests PASS

## Version 0.1.1 — VFS-Grundlage (Vnode + per-Task-FDs)
- [x] `Vnode`/`VnodeOps`-Typen mit Funktionspointer-Tabelle
- [x] Per-Task `FdTable` in `TaskControlBlock` (statt globalem `fd_table[64]`)
- [x] cwd + cwd_vnode in `TaskControlBlock`
- [x] `FileDescription`-Struktur mit Vnode-Zeiger + offset + flags
- [x] Syscall-Implementierung auf Vnode-API umgestellt
- [x] LSEEK(14), IOCTL(15) Syscalls
- [x] strcmp/strncmp zu string.hpp
- [x] 40 Tests PASS

## Version 0.1.2 — Devfs + Mount-System
- [x] Mount-Table (fixed array, longest-prefix-match)
- [x] Pfadauflösung `resolve(path)` — absolut/relativ, `/`, `..`
- [x] Devfs: `/dev/tty`, `/dev/null`, `/dev/console`, `/dev/kbd`
- [x] Blocking-Read auf `/dev/tty` via Scheduler `BLOCKED` + tty_wake_readers()
- [x] initrd als VFS-Filesystem gemountet (`/`)
- [x] open/read/write auf Devices via Vnode-API

## Version 0.1.3 — Procfs + Syscalls
- [x] Procfs: `/proc/meminfo` (dynamisch generiert)
- [x] Procfs: `/proc/[pid]/stat` (Task-Statistiken)
- [x] Procfs: `/proc/self`
- [x] READDIR(16), STAT(17), DUP(18), CHDIR(19) Syscalls
- [x] 50 Tests PASS

## Version 0.1.4 — Blocking-IPC + Service-Vorbereitung (AKTUELL)
- [x] `SEND_SYNC` blockiert Task (BLOCKED) statt busy-wait
- [x] Mailbox-waiting_sender/waiting_receiver Felder
- [x] send()/receive() wecken Wartende
- [x] destroy_mailbox() weckt alle Wartenden
- [x] 54 Tests PASS

## Version 0.2.x — Userspace-Infrastruktur
*Alle Topics rund um Userspace: Shell, fork, Exception-Handling, Task-Isolation*

### Version 0.2.1 — Userspace-Shell + fork
- [x] fork-Syscall (Task-Duplizierung per clone)
- [x] waitpid-Syscall (einschließlich Blocking)
- [x] Userspace-Shell (sh.c) mit cd/export built-ins + exec-extern
- [x] Shell-Pipeline-Unterstützung (|, >, <)
- [x] 100 Tests PASS

### Version 0.2.2 — Pipes + Vnode-Refcounting (AKTUELL)
- [x] PIPE-Syscall (zwei FDs, Ringbuffer in Vnode)
- [x] DUP2-Syscall (fd-Umleitung mit refcount-Inkrement)
- [x] Vnode-Refcounting (refcount-Feld, close nur bei refcount==0)
- [x] WAITPID-Blocking (BLOCKED + EXIT-Weckmechanismus)
- [x] initrd-Vnode-refcount initialisiert (Bugfix: MemPool nullt nicht)
- [x] Userspace-Shell (sh.c) als ELF ladbar + getestet
- [x] 111 Tests PASS

### Version 0.2.3 — Blocking-Mechanisms
- [x] Consider bootparameters for adjustable real time parameters and implement
- [x] Add blocking mechanism: Semaphores, Mutexes, Queues, Task Notification, Event Groups
- [x] Check complete operating system for Priority Inversion and Deadlocks
      — Audit completed: 4 critical priority inversion sites (IPC send_sync, pipe_read, tty/kbd read, scheduler raw priority),
        3 critical deadlock sites (circular send_sync, mailbox use-after-free, pipe dangling pointer),
        7 ad-hoc blocking locations identified for replacement
- [x] Use blocking mechanisms in kernel where ever suitable
      — pipe_read/pipe_write: replaced TaskControlBlock* read_waiter with sync::Semaphore
      — tty_read/kbd_read: replaced raw tty_waiters[] array with sync::Semaphore
      — tty_wake_readers (IRQ context): posts to semaphore instead of raw task state manipulation
      — devfs_init() added for semaphore initialization
- [x] 133 Tests PASS

### Version 0.2.4 — Stable Testenvironment & Debug Engine
- [ ] Fix pre-existing `#GP` crash after 133/133 tests complete (`iretq` bug in ISR common dispatch)
- [ ] Ensure all tests run cleanly without post-test exception
- [ ] Implement macro-based Unified Test Framework (`JARVIS_TEST_SUITE`, `JARVIS_ASSERT_HEX_EQ`) with grep-filterable outputs (`[TEST:RUN]`, `[TEST:PASS]`, `[TEST:FAIL]`)
++ - [ ] Implement static, slab-safe `KernelLogger` supporting multiple log levels (`DEBUG` to `FATAL`) with ANSI-colored serial output via COM1
++ - [ ] Implement robust `kernel_panic` handler providing architectural CPU register dumps (`RAX`, `RIP`, `RSP`, `CR2`, `CR3`) and basic stack frame tracing (`rbp`)
- [ ] Implement robust `kernel_panic` handler providing architectural CPU register dumps (`RAX`, `RIP`, `RSP`, `CR2`, `CR3`) and basic stack frame tracing (`rbp`)
- [ ] move all kernel tests into a boot stage and represent a shell in the user task
- [ ] implement needed tests in user task under a own makefile target
- [ ] implement dependency files in actual Makefile
- [ ] make use of ccache
- [ ] outsource debug symbols
- [ ] implement dead code elemination in linker

### Version 0.2.5 — Exception-Safe Userspace
- [ ] Add setjmp/longjmp recovery for copy_from_user to handle invalid pointers safely
- [ ] User-Exception-Handler: #GP, #PF, #DE, #UD in Ring 3 → User-Exception-Signal (SIGSEGV, SIGFPE, SIGILL) statt Kernel Panic
- [ ] Signal-Dispatch vor Task-Kill: User-Programm erhält Chance auf Cleanup (FDs, Mailbox-Locks)
- [ ] `SYS_KILL(pid)` syscall
- `SYS_GETPID` syscall
- [ ] Task-Tree: Eltern/Kind-Beziehung + `SYS_WAITPID`
- [ ] Task-Exit-Code + Resource-Cleanup bei Terminate (Pages, FDs, Mailboxes)
- [ ] Guard-Pages unter User-Stacks (Stack-Overflow-Erkennung)
- [ ] OOM-Handler: kill low-priority task statt Panic
- [ ] Syscall-Argument-Validation: `CheckedPointer<T>` + `copy_from_user`/`copy_to_user`
- [ ] ELF-Loader: Boundary-Checks für `phdr->offset`, `phdr->filesz ≤ memsz`, Kernel-Bereichs-Grenze
- [ ] VMM: `Page-Ownership`-Check in `free_user_pages` (Bitset: USER/KERNEL)
- [ ] 115 Tests PASS

### Version 0.2.6 — Userspace-Signale + Syscall-Erweiterung
- [ ] Signal-Syscalls (`SYS_SIGNAL`, `SYS_KILL`)
- [ ] Signal-Dispatch bei User-Exceptions (SIGSEGV, SIGFPE, SIGILL)
- [ ] Alarm-Timer pro Task (`SYS_ALARM`)
- [ ] `SYS_GETTOD` (Timer-of-Day)
- [ ] `SYS_UNAME` (System-Info)
- [ ] Userspace sleep() via Alarm-Timer
- [ ] 120 Tests PASS

### Version 0.2.7 — Code Quality & Logging
- [ ] Include the transition to template-based dispatching optimization
- [ ] Syscall-Dispatcher: switch-Block durch Funktionspointer-Tabelle ersetzen
- [ ] `Arch::Serial`-Klasse (Stream-Interface statt bare `outb`-Aufrufe)

- [ ] Type Safety Overhaul: Eliminate naked `reinterpret_cast` logic from high-level kernel flows
  - [ ] Implement explicit `PhysicalAddress` and `VirtualAddress` type wrapper abstractions
  - [ ] Enforce compilation-safe `static_cast` patterns throughout `Vnode` interfaces and Driver registration routines by deploying explicit class hierarchies
  - [ ] Migrate primitive byte-buffer parsing steps (such as parsing `initrd` CPIO header blocks) to safe, alignment-compliant type-punning wrappers
- [ ] `Arch::Serial` class (Stream interface instead of bare `outb` invocations)
- [ ] Logger interface for unified logging (instead of direct `debug_write` calls)
- [ ] Removal of all direct `outb` calls from non-arch code
- [ ] Logger-Interface für einheitliches Logging (anstelle direkter `debug_write`-Aufrufe)
- [ ] Remove all hardcoded values and strings with appropiate enums
- [ ] 125 Tests PASS

## Version 0.3.x — Harte Echtzeit: Scheduler + Timing
*Alle Topics rund um deterministisches Scheduling, Timing-Präzision und WCRT*

### Version 0.3.1 — Präzisions-Timer + Deadline-Monitoring
- [ ] HPET-Timer (Timer-Frequenz bis 10 kHz statt PIT-1 kHz)
- [ ] Deadline-Monitoring: `deadline_ticks` wird überwacht, Overrun-Callback
- [ ] Periodische Task-Release: automatischer Reset von `remaining_ticks` bei Periodenstart
- [ ] WCRT-Analyse: `executed_ticks_max`-Tracking pro Task
- [ ] Scheduler-Statistics-Syscall (`SYS_SCHED_INFO`)
- [ ] `/proc/sched` mit Task-Deadline/Budget/Overrun-Zähler
- [ ] 130 Tests PASS

### Version 0.3.2 — Budget-Enforcement + Synchronisation
- [ ] Add strict lock-hierarchy rules (ID-ordering) for deadlock prevention within PIP
- [ ] Budget-Enforcement: Task wird bei `remaining_ticks == 0` preempted
- [ ] Priority Inheritance Protocol (PIP) für IPC
- [ ] Priority Ceiling Protocol (PCP) für Locking
- [ ] Nested-Lock-Detection (Deadlock-Prävention)
- [ ] Lock-Order-Validator (Debug-Modus)
- [ ] 135 Tests PASS

### Version 0.3.3 — WCET-Analyse + Determinismus
- [ ] Automatische WCET-Messung aller Syscalls (min/max/avg)
- [ ] Stack-Usage-Profiler (max nesting depth pro Task)
- [ ] Determinismus-Review: Alle Allokationen in I/O-Pfaden eliminiert
- [ ] Interrupt-Latenz-Messung (Hardware-timed)
- [ ] Scheduling-Jitter-Messung
- [ ] 140 Tests PASS

## Version 0.4.x — SMP + Multicore
*Alle Topics rund um symmetrisches Multiprocessing und CPU-Affinität*

### Version 0.4.1 — APIC + SMP-Boot
- [ ] Add Per-CPU GDT/TSS isolation and Global Interrupt Spinlocks
- [ ] Local-APIC-Initialisierung (X2APIC mode)
- [ ] I/O-APIC-Initialisierung (Interrupt-Routing)
- [ ] SMP-Boot (APs via SIPI)
- [ ] Per-CPU-Datenstrukturen (PML4, Kernel-Stack, TSS)
- [ ] 145 Tests PASS

### Version 0.4.2 — Per-CPU-Scheduler + Load-Balancing
- [ ] Per-CPU-Run-Queue (kein globaler Lock)
- [ ] Load-Balancing zwischen CPUs (idle-pull / push)
- [ ] CPU-Affinity für Tasks (`SYS_SET_AFFINITY`, `SYS_GET_AFFINITY`)
- [ ] Cache-Coloring (optionale Optimierung)
- [ ] Spinlock/RWLock für SMP-Synchronisation
- [ ] 150 Tests PASS

## Version 0.5.x — Integration + Zertifizierung
*Finalisierung, Stresstests und formale Absicherung*

### Version 0.5.1 — System-Integration
- [ ] Vollständige Integrationstests (Userspace + Kernel kombiniert)
- [ ] 24h Stresstest mit periodischen Tasks (Deadline-Verletzungs-Rate < 0.01 %)
- [ ] Performance-Benchmark-Suite (Syscall-Latenz, IPC-Durchsatz, Kontextwechsel)
- [ ] 155 Tests PASS

### Version 0.5.2 — Sicherheit + Release
- [ ] Add documentation of syscall determinism for certification readiness
- [ ] Sicherheits-Review: Kein Kernel-Pointer-Leak in Userspace
- [ ] Stack-Cookie-Insertion (GCC `-fstack-protector` für Kernel)
- [ ] Release-Build (`-O3 -DNDEBUG`, stripped)
- [ ] Dokumentation finalisiert (Doxygen, Architektur-Overview, WCRT-Report)
- [ ] 160 Tests PASS

## Version 0.6.x — Watchdog + Task-Überwachung
*Hardware-Watchdog, Software-Watchdog pro Task, Deadlock-Erkennung und Recovery*

### Version 0.6.1 — Hardware Watchdog
- [ ] ICH9/HPET Watchdog-Timer-Initialisierung
- [ ] Watchdog-IRQ-Handler (preTimeout/NMI)
- [ ] Watchdog-Reset-Syscall (`SYS_WATCHDOG_KICK`)
- [ ] Boot-Time-Watchdog-Aktivierung (Kernel-Panic bei Ausbleiben von Kicks)
- [ ] Fallback: PIT-basierter Software-Watchdog (falls kein HW-WD)
- [ ] 165 Tests PASS

### Version 0.6.2 — Software Watchdog pro User-Task
- [ ] Per-Task-Watchdog (deadline-basiert: Task muss vor Ablauf der Periodenzeit einen Kick setzen)
- [ ] `SYS_WATCHDOG_CREATE(period_ms)` — Watchdog für aktuelle Task erstellen
- [ ] `SYS_WATCHDOG_KICK` — Watchdog zurücksetzen
- [ ] Watchdog-Überlauf: Task wird auf TERMINATED gesetzt + Recovery-Handler
- [ ] `/proc/[pid]/watchdog` — Status (remaining, overrun_count, last_kick)
- [ ] 170 Tests PASS

### Version 0.6.3 — Deadlock-Erkennung + Recovery
- [ ] Lock-Wait-For-Graph (WFG) für IPC + Mutexe
- [ ] Timeout-basierte Deadlock-Erkennung (Watchdog-gesteuert)
- [ ] Recovery: Task(s) im Deadlock werden terminiert, Ressourcen freigegeben
- [ ] Deadlock-Statistik in `/proc/sched` (Anzahl erkannte Deadlocks, betroffene Tasks)
- [ ] System-Health-Check-Syscall (`SYS_HEALTH_STATUS`)
- [ ] 175 Tests PASS
