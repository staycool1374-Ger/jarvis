# Test Cases — v0.3.4 (Phase 4: Interrupt Latency Jitter)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### APIC Timer Replacement — test_apic_timer.cpp
- Local APIC timer replaces PIT/HPET as system tick source
- APIC timer one-shot mode calibrated to match TICK_HZ
- APIC timer periodic mode fires at correct interval (±1 tick)
- TSC-deadline mode: timer fires at absolute TSC value
- APIC::init() calibrates TSC frequency and configures divider
- Per-CPU APIC timer vector (not shared PIC IRQ0) — each CPU has independent tick
- `CONFIG_USE_APIC_TIMER=0` build: falls back to PIT timer
- IOAPIC redirection entry routes timer IRQ to BSP

### Allocation-free IRQ Paths — test_irq_alloc.cpp
- No dynamic allocation in any IRQ handler
- No allocation in syscall fast-path (getpid, getticks)
- Allocation only in syscall slow-path (open, fork) where documented

### Interrupt Latency Measurement — test_irq_latency.cpp
- Hardware-timed interrupt response time measured via rdtsc
- Latency distribution recorded: min, max, avg, p99
- `CONFIG_IRQ_LATENCY_HISTOGRAM` with 64 buckets (0–100μs)
- ISR entry/exit stubs in isr_stubs.asm save rdtsc immediately (no C++ prologue)
- No interrupt latency exceeds 10 μs on idle system (assert via CONFIG_IRQ_LATENCY_MAX_NS)
- Worst-case latency under synthetic load bounded (< 2× idle latency)
- `CONFIG_IRQ_LATENCY_MAX_NS` assertion fires if exceeded (debug build)

### Jitter Benchmarking — test_jitter.cpp
- Synthetic load: N tasks at same priority
- Schedule-to-schedule jitter measured via rdtsc
- Jitter is deterministic (bounded variance) under fixed load
- Jitter under max load < 2× idle jitter
- Latency distribution recorded per test run

### Deferred Interrupt Handling (Threaded IRQs) — test_threaded_irqs.cpp
- `CONFIG_THREADED_IRQS`: ISR does minimal ack + enqueue to per-IRQ kernel task
- IRQ threads run at fixed configurable priority
- IRQ thread has dedicated stack, no blocking syscalls
- `IrqThread::init(vector, priority, handler)` replaces `IDT::register_handler` for enabled IRQs
- Threaded handler processes IRQ within bounded latency (thread scheduling delay)
- Threaded mode reduces ISR latency for higher-priority IRQs
- `CONFIG_THREADED_IRQS=0` build: legacy direct ISR dispatch retained

### ARM64 Interrupt Controller — test_gic.cpp
- GICv3/GICv4 driver: GICD/GICR/GICC init, priority masking, SGI/PPI/SPI
- Common `ArchInterruptController` interface: init, eoi, mask, unmask, set_priority, get_priority
- GIC timer interrupt (CNTP_EL0) configured and fired
- Interrupt latency measurement on ARM64

### RISC-V64 Interrupt Controller — test_plic.cpp
- PLIC driver: init, priority levels, threshold, claim/complete
- CLINT or SBI timer as system tick source
- Common `ArchInterruptController` interface
- Interrupt latency measurement on RISC-V64
