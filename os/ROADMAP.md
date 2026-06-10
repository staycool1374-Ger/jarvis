# Jarvis RTOS — Sequential Development Roadmap

## Phase 1: Code Refactoring & Microkernel Foundation (0.2.8–0.2.9)

### Version 0.2.8 — Code Refactoring & Streamlining ✓
- [x] **O(1) Syscall Dispatch:** Ersetzung des linearen Syscall-Switch-Verteilers durch eine statische Funktionszeigertabelle zur Eliminierung von Kontextlatenzen.
- [x] **Modern Syscall Entry:** Migration der Syscall-Einsprungsequenz von Legacy `int $0x80`-Software-Interrupts auf native, hochfrequente `syscall/sysret`-Pfade via MSRs (`IA32_STAR`, `IA32_LSTAR`, `IA32_FMASK`). MSR-Infrastruktur (`arch::wrmsr`/`arch::rdmsr`), Syscall::init() konfiguriert alle drei MSRs. `syscall_entry.asm` bereit. Nutzung des `syscall`-Befehls im Userspace für eine Folgesession vorgemerkt (erfordert Korrektur der Registerzuordnung im Entry-Asm und Einrichtung von `KERNEL_GS_BASE`).
- [x] **Strikte Typsicherheit (Type Safety):** Vollständige Eliminierung von rohen `reinterpret_cast`-Konstrukten aus dem Kernel-Core. Implementierung von typsicheren Abstraktions-Wrappern für `PhysicalAddress` und `VirtualAddress` in `address.hpp` (`phys_to_virt`/`virt_to_phys`-Konvertierungen, `PageAddress` für Seiten-aligned-Phys). 
- [x] **Unified Logging & Hardware-Abstraktion:** Einführung einer abstrakten `Arch::Serial`-Hardwareklasse (Ersatz für rohe `outb`-Direktiven) gekoppelt mit einem zentralen `Logger`-Interface. Vollständiger Tree-Purge von Inline-Assembler außerhalb von `arch/` (CR-Lese-/Schreibzugriffe in `arch::read_cr*`/`arch::write_cr3` extrahiert).
- [x] **Linker-Optimierungen:** Integration von Linker-Optimierungen für das Release-Target (`-ffunction-sections`, `-fdata-sections`) zur Ermöglichung effizienter Section-Garbage-Collection in zukünftigen Iterationen. LTO aufgrund fehlender LLD-Integration im Cross-Toolchain zurückgestellt.
- [x] **Code-Qualitäts-Metriken:** Funktionstrennungs-Regel (Syscall-Dateien auf 6 Domänen aufgeteilt). Ersatz von `strncpy` durch `strlcpy` in `sys_umame`.
- [x] **Const Correctness:** Deklaration aller unveränderlichen Parameter und Methoden als `const` in `PhysicalAddress`, `VirtualAddress`, `PageAddress`, der PMM/VMM-Schnittstelle und weiteren Kernkomponenten.
- [x] **Reference Parameters:** Bevorzugung von Referenzparametern (`const T&`) gegenüber Value-Parametern für Nicht-POD-Typen zur Vermeidung unnötiger Kopien und zur Dokumentation von Aliasing-Verträgen.

### Version 0.2.9 — Microkernel Foundation & Task Lifecycle
- [x] **Subsystem Privilege Audit:** Strukturierte Katalogisierung aller Kernel-Subsysteme nach ihren tatsächlichen Privilegien-Anforderungen zur sauberen Trennung von Ring-0- und Ring-3-Inhalten. (`docs/privilege_audit.md`)
- [x] **IPC Latency Benchmark:** Entwicklung einer Mess-Suite (7 kernel-level Mikro-Benchmarks mit RDTSC). Misst IPC send/recv Kosten (avg 235 Zyklen), recv only (50), 64B-Payload (175). Baseline für Microkernel-Vergleich.
- [ ] **Zero-Copy Buffer Pool:** Einführung eines fixen Pools voralozierter 4-KiB-Puffer im Kernelspeicher (~256 KiB, 64 Puffer), die per Handle-Transfer via IPC zwischen Tasks verschoben werden — kein Kopieren der Nutzdaten. Ermöglicht effiziente Pipeline-Kommunikation für VFS- und IOCD-Server und späteres Networking.
  - `BufferPool`-Klasse mit Bitmap-Allocator: `alloc(pid)`, `free(handle)`, `free_all(pid)`-Cleanup bei Task-Tod
  - IPC-Erweiterung: `send_buffer(dest, handle)` und `recv_buffer(handle)` — Handle wird in existierender `Message.data[]` codiert, keine Struct-Vergrößerung
  - Buffer über HHDM direkt adressierbar, keine Page-Table-Manipulation im ersten Schritt
  - 6 Dateien, ~160 Zeilen Implementierung
- [x] **Unprivileged Shell:** Ausführung der Shell als unprivilegierte User-space-Task (`sh.c.elf`, PID 2B, Ring 3). Kernel-Shell als Fallback registriert. Zero additional IPC latency — standard VFS `read("/dev/tty")` und `write()`-Syscalls via MSR-fast-path. (`kernel.cpp:278-295`)
- [ ] **Task Lifecycle Audit:** Systematische Überprüfung ob terminierte Tasks und all ihre Abhängigkeiten (IPC-Queues, Notify, EventGroup, Page-Tables, FDs) vollständig zerstört und aus dem Scheduler entfernt werden. Fix der gefundenen Bugs:
  - `reap_orphans()` can-reap-Logik reparieren (setzt `can_reap=true` bereits beim ersten Loop-Iteration — verhindert korrektes Deferred-Cleanup)
  - `elf::load()` ruft `init_task_common()` nicht auf → `msg_queue`/`notify`/`event_group` sind `nullptr` für ELF-geladene Tasks (IPC-Crash)
  - Blockierte IPC-Sender (`blocked_senders`) werden beim Task-Exit aufgegeben — Tasks bleiben für immer in `BLOCKED`
  - Fork-Kind mit `page_table_shared_=true` ruft `free_user_pages()` auf → Use-After-Free im Parent
- [ ] **Userspace Idle Task:** Implementierung einer einfachen Idle-Task in Ring 3 (`idle.c.elf`), die als niederpriore Hintergrund-Task läuft:
  - Unendliche `pause`-Schleife (userspace-safe, im Gegensatz zu `hlt`)
  - Wird beim Boot in den Scheduler aufgenommen (niedrigste Priorität)
  - Kernel `hlt`-Idle (`tasks_[0]`) bleibt als ultimatives Fallback bestehen
  - Später erweiterbar (Power-Management, CPU-Frequenz-Scaling, Deferred-Work)
- [ ] **User-space VFS Server:** Auslagerung des virtuellen Dateisystems (VFS) aus dem Kernel-Sitz in einen eigenständigen, privilegierten User-space-Serverprozess (`/sbin/vfsd`), der rein über IPC kommuniziert.
- [ ] **User-space Driver Server:** Migration der Tastatur- und seriellen Treiber in einen isolierten User-space-Treiberprozess (`/sbin/iocd`) via IPC.
- [ ] **Capability-basierter Hardware-Zugriff:** Implementierung einer fälschungssicheren Capability-Zugriffskontrolle für Gerätespeicher, sodass MMIO-Regionen exklusiv für autorisierte Treiber-Tasks gemappt werden können.

---

## Phase 2: Filesystems & Shell UX (0.2.10–0.2.11)

### Version 0.2.10 — FAT32 Block Filesystem
- [ ] **ATA PIO Treiber:** Implementierung eines nativen ATA-PIO-Treibers für das QEMU-IDE-Interface (I/O-Ports, Sektorbasiertes Lesen/Schreiben).
- [ ] **Block Device Abstraktionsschicht:** Entwicklung eines standardisierten Block-Layers im VFS zur einheitlichen Verwaltung von Sektoren, Puffern und `ioctl`-Aufrufen.
- [ ] **FAT32 Driver Core:** Implementierung des FAT32-Kerns inklusive MBR-Parsing zur Partitionslokalisierung, FAT-Tabellen-Caching, Cluster-Chain-Traversal und Unterstützung für kurze 8.3-Dateinamen.
- [ ] **Storage Integration:** Erstellung eines 128 MiB FAT32-Disk-Images via `mkfs.fat`, Einbindung in QEMU (`-drive file=disk.img,format=raw,if=ide`) und Bereitstellung als Mountpoint unter `/home`.
- [ ] **VFS FAT32 Operationen:** Ausarbeitung der VFS-Schnittstellen für FAT32 (`open`, `read`, `write`, `close`, `mkdir`, `unlink`, `fstat`, `readdir`), sodass die Shell Dateien im Mount-Pfad manipulieren kann.
- [ ] **Rootfs-Vorbereitung:** Integration architektonischer Hooks, um in zukünftigen Iterationen das Booten des Kernels direkt von einer FAT32-Partition anstelle der Initrd zu ermöglichen.

### Version 0.2.11 — Shell UX & Utilities
- [ ] **Persistent Status Bar:** Implementierung einer permanenten Statuszeile im Framebuffer-Terminal (invertierte Textzeile am unteren Rand) und im seriellen Terminal (ANSI-Escape-Sequenzen) zur Live-Anzeige von Datum/Zeit, CPU%, Speicher% und dem Exit-Code des letzten Befehls.
- [ ] **Prompt-Erweiterung:** Upgrade des Shell-Prompts zu einem dynamischen, Zsh-ähnlichen Format (`user@host:/pwd$`) mit nativer Aktualisierung bei Verzeichniswechseln und Unterstützung einer konfigurierbaren `PROMPT`-Umgebungsvariable.
- [ ] **Erweiterung der Shell-Builtins:** Direktes Einbetten der Befehle `help`, `echo`, `pwd`, `clear`, `which`, `env` und `sleep` als integrierte Shell-Routinen.
- [ ] **Verzeichnis-Syscalls:** Bereitstellung der Systemaufrufe `SYS_MKDIR` und `SYS_UNLINK` im Kernel zur Unterstützung externer Dateimanipulationen.
- [ ] **Freistehende Initrd-Utilities:** Kompilierung von sauberen, eigenständigen User-space-Binärdateien für `mkdir`, `rm`, `rmdir`, `echo`, `pwd`, `clear` und `sleep` innerhalb des Initrd-Abbilds.
- [ ] **IPC Pipeline-Robustheit:** Härtung und Fehlerbereinigung der Shell-Ausführungsschleife bei komplexen Verkettungen (Pipes `|` und Ein-/Ausgabeumleitungen `>`, `<`).

---

## Phase 3: System Services & Hardware (0.2.12–0.2.14)

### Version 0.2.12 — System Services
- [ ] **tmpfs (Temporäres Dateisystem):** Entwicklung eines flüchtigen, RAM-basierten Dateisystems mit dynamischer Page-Allokation und harten, benutzerspezifischen Quotenlimits zur Vermeidung von OOM-Exploits. Mounten unter `/tmp` während des Boots.
- [ ] **Init-System (PID 1):** Entwurf von `/sbin/init` als primäre User-space-ELF-Task, die den Service-Lebenszyklus überwacht, Abstürze abfängt und den Bootvorgang über strukturierte `/etc/rc`-Skripte steuert.
- [ ] **Automated Mounts (fstab):** Spezifikation und Parser-Implementierung für eine `/etc/fstab`-Konfigurationsdatei zur automatisierten Einbindung aller Systemdateisysteme vor dem Shell-Start.
- [ ] **Ressourcenlimits & Heap-Erweiterung:** Implementierung von `SYS_GETRLIMIT`/`SYS_SETRLIMIT` im TCB (Überwachung von `max_fds`, `max_stack`, `max_memory`) sowie Einbindung von `SYS_BRK` zur dynamischen Heap-Vergrößerung (`sbrk`/`brk` in der libc).
- [ ] **Core Text Utilities:** Bereitstellung eines schlanken, interaktiven Text-Pagers (analog zu `less`) und eines zeilenbasierten Editors (analog zu `ed`) als native System-Binaries.

### Version 0.2.13 — Hardware Enablement
- [ ] **PCI-Enumeration:** Implementierung des Zugriffs auf den PCI-Konfigurationsraum (I/O-Ports `0xCF8`/`0xCFC`), Scannen von Bus 0, Parsing von Capability-Listen (MSI, MSI-X, PM) und dynamische BAR-Ressourcenallokation.
- [ ] **Virtio Transport & Drivers:** Entwicklung der Virtio-Transportschicht (`virtio-mmio` oder `virtio-pci`) inklusive Descriptor-Ringen. Schreiben eines hochperformanten `virtio-blk`-Treibers als FAT32-Backend-Alternative.
- [ ] **Minimaler Netzwerk-Stack:** Implementierung eines komprimierten, echtzeitfähigen Netzwerk-Protokollstacks für ARP, IPv4 und UDP direkt über ein emuliertes `virtio-net`-Gerät.
- [ ] **FPU/SSE-Kontextwechsel:** Integration eines verzögerten (Lazy) FPU/SSE-Status-Backups via `FXSAVE`/`FXRSTOR` über einen dedizierten 512-Byte-Puffer im `TaskControlBlock` bei jedem Thread-Wechsel.
- [ ] **Entropie & Krypto-RNG:** Einbindung hardwarebasierter Zufallsgenerierung (`RDRAND`/`RDSEED`) gekoppelt mit einem Software-ChaCha20-PRNG, exportiert über `/dev/random` und `/dev/urandom` via Devfs sowie über `SYS_GETRANDOM`.

### Version 0.2.14 — Observability & Portability
- [ ] **Kernel Log Ringbuffer:** Erstellung eines persistenten, fixierten In-Memory-Ringpuffers für alle Log-Einträge des Kernels, auslesbar im User-space über den neuen Syscall `SYS_KLOG` und ein zugehöriges `dmesg`-Utility.
- [ ] **Hardware Abstraction Layer (HAL):** Formale Kapselung hardwarespezifischer Logik hinter einem sauberen Schnittstellen-Framework in `arch/hal.hpp` (`ArchPageTable`, `ArchContext`, `ArchInterruptController`, `ArchTimer`, `ArchIO`).
- [ ] **Strukturelle Verzeichnis-Migration:** Verschiebung des gesamten x86_64-spezifischen Codes (inklusive der Syscall-Einsprungspunkte) aus dem generischen `arch/`-Pfad in das dedizierte Unterverzeichnis `arch/x86_64/`.
- [ ] **Multi-Arch-Buildsystem:** Erweiterung des Makefiles um eine globale `ARCH`-Variable zur einfachen Steuerung von Cross-Compiler-Toolchains (Standard: x86_64).
- [ ] **Secure Exec & Regressions-Audit:** Refactoring von `SYS_EXEC` zur elementweisen Überprüfung von `argv` und `envp` über ein `CheckedPointer<T>`-Konstrukt (Rückgabe von `-EFAULT` statt Kernel Panic). Validierung aller 492+ Regressionstests nach dem HAL-Umbau.

---

## Phase 4: Hard Real-Time (0.3.x)

### Version 0.3.1 — High-Precision Timers & Deterministic Memory
- [ ] **O(1) Bitmap Scheduler:** Ablösung des linearen `tasks_[64]`-Array-Scans durch eine Bitmap-indizierte, strikt prioritätsgeordnete per-priority Queue-Struktur. `next_task()` von O(n) auf O(1) reduziert mittels `__builtin_clzll(occupied_)`. `needs_switch()` von O(n) auf O(1) durch Bitmasken-Vergleich. Round-Robin innerhalb gleicher Priorität. Voraussetzung für WCRT-Analyse, Deadline-Monitoring und Priority Inheritance.
  - Einführung `sched_next`-Zeiger im `TaskControlBlock`
  - Neue Scheduler-Zustände: `queue_heads_[64]`, `queue_tails_[64]`, `occupied_`-Bitmap, `current_task_`-Pointer
  - Rewrite von `add_task`, `remove_task`, `next_task`, `needs_switch`, `reap_orphans`
  - Ersatz von `task_at(i)`-Iterationen in externen Callern durch `for_each_task(callback)`
- [ ] **HPET-Treiber:** Implementierung eines Treibers für den High Precision Event Timer zur Anhebung der Kernel-Taktfrequenz auf hochpräzise 10 kHz (Ablösung des unpräzisen 1 kHz PIT).
- [ ] **Deadline-Überwachung:** Einführung strikter Echtzeit-Fristenprüfungen anhand von `deadline_ticks`-Grenzen, gepaart mit einer asynchronen Overrun-Callback-Infrastruktur.
- [ ] **Periodische Release-Kontrollen:** Integration automatisierter Scheduler-Schleifen zur präzisen Freigabe periodischer Tasks und zum Zurücksetzen des verbleibenden Budgets (`remaining_ticks`).
- [ ] **WCRT-Analyse:** Einbettung analytischer Profiler zur Ermittlung der Worst-Case Response Time (WCRT) durch Erfassung der maximalen Ausführungsspitzen (`executed_ticks_max`) je Task.
- [ ] **Scheduler-Observability:** Bereitstellung von Echtzeit-Leistungsdaten über den Systemaufruf `SYS_SCHED_INFO` und Abbildung der Task-Metriken im synthetischen Dateisystem unter `/proc/sched`.
- [ ] **Deterministische User-space-Pools:** Bereitstellung vorallozierter User-space-Speicherpools (Slab/Buddy-Strukturen) zur Gewährleistung von $O(1)$-Allokationen im Real-Time-Kontext ohne die Gefahr von lazy PMM-Kernel-Fallbacks.

### Version 0.3.2 — Budget Enforcement & Synchronization Protocols
- [ ] **Statische Ressourcen-Hierarchie:** Erzwingung einer strikten Lock-Reihenfolge über vordefinierte, statische Ressourcen-IDs zur mathematischen Eliminierung von Deadlocks in verschachtelten Sperr-Szenarien.
- [ ] **Hardwarebasiertes Budget-Enforcement:** Konfiguration des Hardware-Timers zur sofortigen, harten Task-Präemption exakt in dem Moment, in dem das zugewiesene Thread-Budget `remaining_ticks == 0` erreicht.
- [ ] **Vollständiges PIP (Priority Inheritance):** Implementierung des vollständigen Prioritätsvererbungsprotokolls über alle internen IPC-Transaktionsschichten und verschachtelten Task-Abhängigkeitspfade hinweg.
- [ ] **Vollständiges PCP (Priority Ceiling):** native Unterstützung des Priority-Ceiling-Protokolls für Kernel-Mutex-Objekte zur strukturellen Verhinderung von Prioritätsinversionen und Deadlocks vor deren Entstehung.
- [ ] **Asynchroner Lock-Validierungs-Engine:** Entwicklung eines zur Laufzeit im Debug-Build aktiven Prüf-Algorithmus zur Erkennung illegaler, zyklischer Sperranforderungen.
- [ ] **Echtzeit-Memory-Pinning:** Implementierung der Systemaufrufe `SYS_MLOCK` und `SYS_MLOCKALL`, um die physischen Pages geschützter Echtzeit-Tasks dauerhaft im RAM zu verankern und Page-Fault-Latenzen vollständig auszuschließen.

### Version 0.3.3 — WCET Analysis & Tuning
- [ ] **Syscall WCET Tracking:** Integration automatisierter Monitore zur präzisen Berechnung der Worst-Case Execution Time (WCET) über sämtliche offengelegten Systemaufrufe (Erfassung von Min/Max/Avg-Ausführungszeiten).
- [ ] **Kernel Stack Usage Profiler:** Entwicklung einer Hintergrund-Task zur kontinuierlichen Überwachung und Aufzeichnung der maximalen Stack-Nestingtiefe pro Ausführungsthread.
- [ ] **Allokationsfreie IRQ-Pfade:** Vollständige Eliminierung jeglicher dynamischer Heap-Allokationen innerhalb kritischer E/A- und Interrupt-Verarbeitungspfade zur Sicherung der zeitlichen Vorhersehbarkeit.
- [ ] **Interrupt-Latenz-Messung:** Implementierung hardwaregetakteter Messroutinen mittels CPU-Zykluszählern zur Erfassung der exakten Hardware-Interrupt-Antwortzeit-Varianz.
- [ ] **Jitter-Benchmarking-Suite:** Erstellung eines synthetischen Last-Szenarios zur kontinuierlichen Quantifizierung des Scheduling-Jitters unter maximaler Systembelastung.

---

## Phase 5: SMP + Multicore (0.4.x)

### Version 0.4.1 — APIC, SMP Boot & IRQ Priority
- [ ] **Per-CPU Datenisolation:** Instanziierung isolierter GDT- und TSS-Strukturen pro CPU-Kern, abgesichert durch globale, hochoptimierte Spinlock-Barrieren.
- [ ] **Lokale APIC-Initialisierung:** Aktivierung und Konfiguration des Local APIC (inklusive X2APIC-Topologie) auf dem Bootstrap-Prozessor (BSP) für feingranulares Interrupt-Routing.
- [ ] **I/O APIC Routing:** Konfiguration des E/A-APIC zur deterministischen Verteilung externer Hardware-Interrupt-Vektoren auf dedizierte CPU-Kerne.
- [ ] **Multicore-Symmetric-Boot:** Implementierung der SMP-Startsequenz zum kontrollierten Aufwecken der sekundären Application Processors (APs) mittels der standardisierten INIT-SIPI-Inter-Processor-Interrupt-Folge.
- [ ] **Core-State-Isolation:** Physische Trennung der Systemtabellen durch Zuweisung individueller PML4-Seitenverzeichnisse, Kernel-Stacks und TSS-Deskriptoren je aktivem Core.
- [ ] **Hardware-Interrupt-Priorisierung:** Nutzung der APIC Task Priority Register (TPR), um eine strikte Hardware-Interrupt-Prioritätsebene einzuziehen, die das Unterbrechen kritischer Tasks durch niederpriorisierte IRQs blockiert.

### Version 0.4.2 — Per-CPU Scheduling & Load Balancing
- [ ] **Verteilte Run-Queues:** Architektur-Redesign des Schedulers weg von einer zentralen Sperre hin zu autarken, isolierten Run-Queues pro CPU-Kern zur Skalierungs-Optimierung.
- [ ] **Echtzeit-Lastverteiler:** Integration symmetrischer Load-Balancing-Algorithmen, die mittels Idle-Pull- und Work-Push-Metriken Threads deterministisch über die Cores verteilen.
- [ ] **Core-Affinität:** Bereitstellung der Systemaufrufe `SYS_SET_AFFINITY` und `SYS_GET_AFFINITY` im Kernel und Einbettung der entsprechenden Wrapper in die libc zur gezielten Task-Bindung.
- [ ] **Cache-Coloring-Allokator:** Entwicklung eines cache-sensitiven Page-Allocators zur Minimierung zerstörerischer L2/L3-Cache-Line-Evictions bei paralleler Kernauslastung.
- [ ] **SMP-Synchronisations-Primitives:** Verteilung hocheffizienter, hardwarenaher SMP-Spinlocks und Reader-Writer-Locks im gesamten Kernel-Baum zum Schutz konkurrierender Datenströme.
- [ ] **Real-Time SMP Verification Audit:** Vollständige mathematische Re-Verifikation und messtechnische Auditierung aller einkernigen WCET- und WCRT-Grenzen unter aktiver SMP-Lock-Konkurrenz und maximaler Bus-Last.

### Version 0.4.3 — TLB Shootdown Optimization
- [ ] **PCID-Unterstützung:** Aktivierung der Process Context Identifiers (PCID) in den Steuerregistern, um das vollständige Verwerfen der TLB-Einträge bei Adressraumwechseln zu verhindern.
- [ ] **Selektive TLB-Invalidierung:** Nutzung des `INVPCID`-Befehlssatzes zur gezielten, feingranularen Löschung spezifischer Pages anstelle eines globalen TLB-Flushes.
- [ ] **Lazy TLB Shootdowns:** Implementierung eines verzögerten (Lazy) TLB-Shootdown-Verfahrens für Pages, die über mehrere Cores hinweg in Shared-Memory-Szenarien gemappt sind.
- [ ] **IPI-Vermeidungs-Strategien:** Entwicklung von Batching- und Deferral-Strategien, um die Anzahl der teuren Inter-Processor-Interrupts (IPIs) während eines TLB-Shootdowns drastisch zu reduzieren.
- [ ] **Latenz-Profilierung:** Einbau automatisierter Metriken zur exakten Gegenüberstellung der gemessenen TLB-Shootdown-Latenz im Verhältnis zur Anzahl der abgesetzten Cross-Core-IPIs.
- [ ] **PML4-Synchronisations-Integration:** Tiefe Verankerung der Shootdown-Optimierungsschicht direkt innerhalb der per-CPU PML4-Aktualisierungsroutinen des Kernels.

---

## Phase 6 & 7: System Integration & Safety Systems (0.5.x–0.6.x)

- **0.5.1 System Integration Testing:** Bereitstellung umfassender Test-Suites für Cross-Boundary-Flows; Durchführung eines 24-Stunden-Dauerstresstests unter Sicherstellung einer Deadline-Overrun-Rate von < 0.01%; Benchmark-Erfassung von IPC-Durchsatz und Context-Switches.
- **0.5.2 Safety Hardening & Release:** Dokumentation des Syscall-Determinismus für Sicherheitszertifikate; Zeiger-Isolations-Audit; Einschleusen von Stack-Cookies via GCC `-fstack-protector`; Erstellung optimierter Release-Builds (`-O3 -DNDEBUG`, stripped); Doxygen-Export.
- **0.6.1 Hardware-Watchdog:** Initialisierung nativer ICH9/HPET-Watchdog-Schaltkreise; Implementierung von NMI-Dienstroutinen für Pre-Timeout-Warnungen; Bereitstellung von `SYS_WATCHDOG_KICK`; PIT-Software-Fallback bei fehlender Hardware.
- **0.6.2 Per-Task Software Watchdog:** Implementierung eines softwarebasierten Überwachungssystems auf Task-Ebene über `SYS_WATCHDOG_CREATE(period_ms)` und `SYS_WATCHDOG_KICK`; automatische Task-Terminierung bei Ablauf; Zustands-Export via `/proc/[pid]/watchdog`.
- **0.6.3 Deadlock-Erkennung & Recovery:** Laufzeit-Generierung dynamischer Ressourcen-Abhängigkeitsgraphen (Wait-For-Graphs); non-blocking Erkennungsschleifen über die Watchdog-Infrastruktur; gezielte Isolation und Zwangs-Terminierung blockierter Threads samt Ressourcen-Reclamation; Bereitstellung von `SYS_HEALTH_STATUS`.
