# Test Cases — v0.4.3 (Phase 5: TLB Shootdown Optimization)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### PCID Support — test_pcid.cpp
- PCID (Process Context Identifier) enabled in CR4
- TLB entries tagged with PCID on context switch
- Address space switch with same PCID invalidates only matching entries
- PCID rollover handled (all 4096 IDs exhausted)

### Selective TLB Invalidation — test_invpcid.cpp
- `INVPCID` with individual-address mode invalidates single page
- `INVPCID` with single-context mode invalidates all entries for one PCID
- `INVPCID` with all-context mode invalidates global entries
- Non-existent address ignored by INVPCID

### Lazy TLB Shootdown — test_lazy_tlb.cpp
- Remote TLB invalidation deferred until next context switch
- Lazy shootdown coalesces multiple invalidations
- Memory corruption prevented despite lazy invalidation
- Lazy timeout forces flush if no context switch occurs

### IPI Batching — test_ipi_batching.cpp
- Multiple pending TLB flushes batched into single IPI
- Batch processing order is deterministic
- IPI count per test is less than unbatched version
- Batch size does not overflow IPI message buffer

### Latency Profiling — test_tlb_latency.cpp
- Per-TLB-flush latency recorded
- Average latency < unbatched baseline
- P99 latency bounded (< 2x average)
- Latency proportional to number of active CPUs

### PML4 Synchronization — test_pml4_sync.cpp
- Page table update on one CPU visible to all CPUs
- Shootdown completes before write is considered visible
- Shared page modification (fork) synchronized across CPUs
- Page removal invalidated on all CPUs before page reuse
