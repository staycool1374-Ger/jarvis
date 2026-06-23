# Test Cases — v0.2.19 (Kernel Memory Safety Audit + Multi-Arch Infrastructure)

### Resource Tracker — ResourceTracker & test_isolate.cpp
- Memory leak detection across all kernel resource types — PMM pages, MemPool blocks, TCBs, IPC objects, buffer pool pages, VFS vnodes/FDs
- Snapshot/restore zero-delta verification — every test must restore all tracked resources to baseline

### RAII/Cleanup — ScopeGuard, UniquePtr, test_resource_exhaustion.cpp
- Stack-to-bss relocation — `PmmExhaustion` 100K-element page tracking array moved off 64KB kernel stack (fixes stack-overflow triple-fault hang at test #438)
- Progress markers during long-running allocation loops — host watchdog 10s stall trigger suppressed
- ScopeGuard on early-return paths — cleanup guaranteed in buffer_pool, starvation_deadlock, preemption_under_syscall, spinlock_stress, atomic_context_switch, vfs_fat32 tests

### Allocator — operator new/delete, MemPool, PMM fallback
- Three-tier fallback: MemPool → PMM pages (with PmmAllocHdr) → bump allocator
- MemPool `contains()`/`is_ready()` gating — safe before MemPool::init()
- `delete` handles MemPool/PMM, no-ops on bump pointers (early-boot system-lifetime allocations)

### Multi-Arch Infrastructure
- Renode v1.16.1 platform descriptions: x86_64 (SeaBIOS + NS16550), aarch64 (Cortex-A53 + PL011 + GICv3), riscv64 (RV64 + NS16550 + PLIC + CLINT)
- Cross-compilers: aarch64-elf-gcc, riscv64-elf-gcc
- Makefile targets: `make run-renode` (RENODE_ARCH=), `make renode-test`
- CI pipeline with Renode install and platform validation
