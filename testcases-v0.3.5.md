# Test Cases — v0.3.5 (Phase 4: Deterministic Memory & Resource Management)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Static Memory Pools — test_static_pools.cpp
- `CONFIG_STATIC_POOLS_ONLY`: disable PMM::alloc_page() after kernel::init() completes
- Pre-allocate ALL kernel structures at boot: TCB array, page tables, IPC buffers, driver rings
- `MemPool::reserve(pool_idx, count)` called once at init, fails if insufficient
- Allocation after init returns from pre-allocated pool, not general PMM
- Pool exhaustion returns error (graceful) not nullptr — verify handler fires
- `CONFIG_STATIC_POOLS_ONLY=0`: dynamic allocation allowed (soft-RT compatibility)

### Kernel Stack Usage Profiler — test_stack_profiler.cpp
- Background task records max stack depth per thread
- Stack depth increases with call nesting
- Stack usage resets on thread exit
- Profiler overhead does not skew measurements

### Stack Allocation — test_stack_alloc.cpp
- `CONFIG_TASK_STACK_SIZE` per-priority array in config
- Stack guard page (unmapped) below each kernel stack — page fault on overflow
- `CONFIG_STACK_OVERFLOW_HOOK`: weak symbol called from #PF handler on overflow
- Stack overflow triggers hook, not silent corruption
- compile-time stack usage analysis: `-fstack-usage` + script verifies ≤ `CONFIG_TASK_STACK_SIZE`

### Page Tables — test_page_tables.cpp
- Remove clone() page-table sharing (`page_table_shared_`) — full copy or static assignment
- `CONFIG_MAX_PROCESS_PAGES`: bound user page tables per task
- Pre-allocate page-table pages from dedicated PMM pool (no general PMM alloc in hot path)
- Page-table fork does not exceed configurable page budget
- Shared page (PDPT/PD/PT) corruption cannot cross tasks after fix

### Buffer Pool / IPC — test_buffer_pool_deterministic.cpp
- `BufferPool`: pre-allocated ring of fixed-size buffers, no alloc/free after init
- `CONFIG_BUFFER_POOL_BLOCKS` per size class
- IPC messages: inline payload (no heap), buffer handle for zero-copy
- Buffer exhaustion returns error (blocking or ENOSPC) — no dynamic fallback
- Zero-copy buffer transfer: sender gives buffer, receiver consumes, no copy

### Eliminate operator new/delete — test_no_op_new.cpp
- All kernel allocations use `MemPool::alloc/free` or placement new into static storage
- `-fno-exceptions -fno-rtti -fno-threadsafe-statics` in kernel CFLAGS
- Audit lib/stdcpp.cpp: `::operator new` implementations removed
- No dynamic allocation in any kernel code path after init
