# Jarvis RTOS — Development Roadmap

**Build:** v0.3.6-dev | **Last Release:** v0.3.5

## Safety & Concurrency Guardrails (Strict)
- **Transition to Fine-Grained Locks:** All new synchronization code must use `SpinLock` + `SpinLockGuard` for short critical sections and `sync::Mutex` (without IrqGuard) for blocking paths. The global `IrqGuard` is deprecated for all uses except boot, panic, and test isolation.
- **Reference-Enforced Tasks:** When manipulating task blocks or IPC endpoints within the new init system or system calls, strictly enforce reference passing over raw pointers to prevent dangling lookups.
- **Zero-Allocation tmpfs Operations:** Ensure the initial `tmpfs` implementation relies on the pre-existing fixed `MemPool` / `BufferPool` infrastructure for its nodes to avoid unbounded allocations that violate resource tracking limits.

## Active Development — v0.3.6

### Remaining Work
- [ ] **HHDM snapshot restore** — PD save/restore for PDPT[0] (see `docs/hhdm-snapshot-restore.md`)
- [ ] **Re-enable `vmm_huge_page_split_regression` / `vmm_hhdm_access_consistency`** — blocked by HHDM snapshot restore
- [ ] **Consolidate `all` class** — once HHDM tests pass, remove `all-1` / `all-2` split
- [ ] **pml4_clone test class crashes** — Page Fault after `pml4_fork_no_child_corrupt_parent` (test 485) and `pml4_free_user_pages_shared_safe` (test 486) with CR3 corruption; pre-existing, blocked by HHDM snapshot restore

### Stack Guard & Fork (Deferred)
- [ ] Stack guard page via private VA window (requires snapshot-safe page table pool)
- [ ] `page_table_shared_` removal — complete deep-copy fork (walk all user entries, allocate new PDPT/PD/PT, copy contents). Current state: config + pool done.

## Past Releases

See `ROADMAP_done.md` for completed items in released versions (v0.2.x — v0.3.5).
