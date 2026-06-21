# Mandelbrot CPU-Benchmark

**Algorithmus**: Mandelbrot 256×256 (Schritt 2 → 128×128 = 16384 Pixel), 64 Iterationen max, 100 Wiederholungen

**Checksum**: `6393500`

| System | Zeit | vs Linux | Anmerkung |
|--------|------|----------|-----------|
| **Linux** (Ubuntu, Kernel 6.x) | **29 ms** | 1.00× | g++ 15.2, `-O3`, native i7-3930K |
| **Jarvis RTOS** (QEMU + KVM) | **35 ms** | 1.21× | `-O3`, `-mgeneral-regs-only` (kein SSE/AVX), QEMU-VMexit-Overhead |
| **Jarvis RTOS** (QEMU, kein KVM) | **119 ms** | 4.10× | reine Software-Emulation |

## Hardware

- **CPU**: Intel Core i7-3930K @ 3.20 GHz (6 Kerne / 12 Threads, Sandy Bridge-E)
- **RAM**: 32 GB DDR3
- **QEMU**: 256 MB RAM, 1 vCPU

## Analyse

- Jarvis RTOS erreicht **~83% der nativen Linux-Performance** bei Ausführung auf KVM
- Der Geschwindigkeitsunterschied (35 vs 29 ms) ist erklärbar durch:
  - **Kein SSE/AVX**: `-mgeneral-regs-only` verhindert SIMD-Nutzung; Linux verwendet automatisch SSE/AVX-Instruktionen
  - **QEMU-KVM-VMexit-Overhead**: PIT-Interrupts (1000 Hz) verursachen regelmäßige VM-Exits (~alle 1 ms)
  - **TLB/Memory-Overhead**: Emulierte HW hat höhere Latenzen bei Pagetable-Zugriffen
- Die Berechnung ist **integer-basiert** (keine Fließkommazahlen) → Vergleich fair
