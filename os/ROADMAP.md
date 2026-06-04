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

## Version 0.0.6 — Finale Optimierung & Stabilität (AKTUELL)
- [ ] Vollständiger Clean-Code-Review
- [ ] Letzte Optimierungsrunde
- [ ] Ausstehende Bugfixes

## Version 0.0.7 — Optimierung auf -O3
- [x] Build auf -O3 umgestellt
- [x] Keine Compiler-Warnungen unter -O3
- [x] Keine Compiler-Fehler unter -O3
- [ ] Performanceregressionen analysieren

## Version 1.0.0 — Finale Version
- [ ] Stabilitäts-Check
- [ ] Vollständige Integrationstests
- [ ] Release-Build
- [ ] Dokumentation finalisiert
