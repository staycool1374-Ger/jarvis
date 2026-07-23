# Test Cases — v0.3.10 (Phase 4: Documentation & Certification Artifacts)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### WCET Analysis Report — test_wcet_report.cpp
- `docs/wcet_analysis.md` generated with measured max cycles per kernel function
- Toolchain: `objdump -d` + static analysis (aiT, OTAWA, or custom script)
- WCET report covers: scheduler dispatch, IPC send/recv, syscall entry/exit, IRQ entry/exit, MemPool alloc/free
- Each WCET figure includes test environment (QEMU or hardware, CPU model, clock speed)
- WCET figures traceable to specific test invocation

### Safety Manual — test_safety_manual.cpp
- `docs/safety_manual.md` documents: assumptions, limitations, configuration rules for ASIL D
- Safety manual covers: scheduler invariants, memory isolation, interrupt latency bounds, watchdog coverage
- Configuration rules specify mandatory settings for each safety level
- Known limitations documented (e.g., OOM policy gap, single-core assumption)

### Traceability Matrix — test_traceability.cpp
- `docs/traceability.csv`: each ISO 26262-6 requirement → design element → code module → test case
- Every requirement in safety manual has at least one test case mapped
- Traceability matrix is machine-readable (CSV)
- CI job validates: no test in matrix without existing test registration
