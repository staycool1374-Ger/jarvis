[>] Running Agent 3: Kernel Synthesizer...
# JARVIS RTOS — KERNEL MEMORY SUBSYSTEM AUDIT
**Reviewer:** Lead Kernel Architect
**Scope:** `src/kernel/memory/{integrity,markers,mempool,pmm,vmm}.cpp/.hpp`
**Disposition:** Attacker report reviewed against source. All 11 findings verified as technically real defects in the reviewed code (no fabricated/false-positive findings identified). Severity and root-cause language have been corrected where the attacker cited unverifiable "module spec" text; findings are re-grounded strictly in observable code behavior. Output below is the authoritative fix list for the code-generation agent.

---

## [ ] VULN-001 — Out-of-Bounds Bitmap Access in `MemPool::Pool` (Heap Corruption)

**FILE / LOCATION:**
- `src/kernel/memory/mempool.hpp` — `struct Pool { uint64_t freed_bitmap[4]; uint64_t pinned_bitmap[4]; }` and `struct PoolMeta { uint64_t freed_bitmap[4]; }`
- `src/kernel/memory/mempool.cpp` — `MemPool::init()` (`counts[POOL_COUNT] = {256,128,320,32,16,8,16,64,64}`), and all callers of `is_block_freed/set_block_freed/clear_block_freed/is_block_pinned/set_block_pinned/clear_block_pinned`

**ROOT CAUSE:**
Pool class index 2 has `block_count = 320`, but `freed_bitmap`/`pinned_bitmap` are sized `[4]` (256 bits). `is_block_freed(idx)` computes `freed_bitmap[idx/64]`; for `idx = 256..319`, `idx/64 == 4`, which is one element past the array bound. This is executed unconditionally inside `MemPool::init()`'s block-initialization loop (`for j in 0..block_count`) on every boot, making it a guaranteed out-of-bounds write on cold start, not a corner case. It silently overwrites adjacent static storage (`pinned_bitmap[0]` in the `Pool` struct, and out-of-struct memory in `PoolMeta`). This is undefined behavior and directly violates ASIL-D freedom-from-interference: allocator metadata corruption is unbounded-impact and non-deterministic.

**REQUIRED FIX:**
1. In `mempool.hpp`, define `static constexpr size_t MAX_BLOCKS_PER_POOL = 320;` and `static constexpr size_t BITMAP_WORDS = (MAX_BLOCKS_PER_POOL + 63) / 64;` (evaluates to 5).
2. Resize `freed_bitmap[BITMAP_WORDS]`, `pinned_bitmap[BITMAP_WORDS]` in `Pool`, and `freed_bitmap[BITMAP_WORDS]` in `PoolMeta`.
3. Update `copy_freed_bitmap`/`write_freed_bitmap`/`clear_pinned_bitmap` loops from hardcoded `4` to `BITMAP_WORDS`.
4. In `mempool.cpp`, add `static_assert` (or a boot-time `ENSURE` inside `MemPool::init()`) that `counts[i] <= MAX_BLOCKS_PER_POOL` for all `i`, so any future pool-size change that exceeds the bitmap capacity fails to compile/boot rather than silently corrupting memory.
5. No dynamic allocation — all arrays remain fixed-size `constexpr`-bounded, freestanding-compatible.

---

## [ ] VULN-002 — Unsynchronized Mutation of Shared PMM Bitmaps and MemPool Free-Lists

**FILE / LOCATION:**
- `src/kernel/memory/pmm.cpp` — `bitmap_set/bitmap_clear/bitmap_test`, `owner_set_user/owner_set_kernel`, `try_alloc_kernel`, `try_alloc_user`, `alloc_page`, `alloc_contiguous`, `alloc_user_page`, `alloc_user_contiguous`, `alloc_page_table`, `free_page`, and all `_err` variants
- `src/kernel/memory/mempool.cpp` — `MemPool::alloc`, `MemPool::free`, `MemPool::alloc_err`, `MemPool::free_err`, `MemPool::reserve`, `MemPool::pin_block`, `MemPool::unpin_block`

**ROOT CAUSE:**
None of the listed functions acquire any lock or disable interrupts before mutating shared static state (`bitmap_`, `owner_bitmap_`, `free_pages_`, `pool.first_free`, `pool.free_count`, embedded free-list pointers). Given this is a preemptible/interruptible RTOS kernel (evidenced by `Scheduler`, `arch::hlt`, IRQ-driven page faults elsewhere in the codebase), any allocation/free triggered from an ISR context or preempted mid-update produces a torn read-modify-write on the bitmap or free-list head, resulting in double-allocation of the same physical page/block or permanently lost free entries. This is a data race with unbounded corruption blast radius — unacceptable for ASIL-D.

**REQUIRED FIX:**
1. Introduce (or reuse if it exists elsewhere in the tree) a freestanding `kernel::SpinLock` (ticket or TAS-based, no heap allocation, `constinit`-initializable, no libc dependency).
2. Add one static `SpinLock` per critical section domain: `pmm_lock_` guarding `bitmap_`/`owner_bitmap_`/`free_pages_`/pool-generation fields in `pmm.cpp`; one lock per `Pool` (or a single global `mempool_lock_`) in `mempool.cpp`.
3. Wrap every public mutating entry point (all functions listed above) in an RAII guard that (a) saves/disables IRQs via `arch::irq_save()`/`arch::irq_restore()`, then (b) acquires/releases the spinlock — e.g. `IrqSpinLockGuard guard(pmm_lock_);` at function entry, scope-exit unlock.
4. Keep critical sections minimal — do not hold the lock across `oom_handler_()` callback invocation (release lock before calling handler, since it may itself allocate/log/panic); re-acquire for the retry.
5. Zero heap allocation: `SpinLock` must be a POD-like class with only atomic/volatile integer state, no constructors requiring dynamic init beyond `constinit`.

---

## [ ] VULN-003 — Non-Deterministic (Linear-Scan) Physical Page Allocation

**FILE / LOCATION:**
`src/kernel/memory/pmm.cpp` — `PMM::try_alloc_kernel(size_t count)`, `PMM::try_alloc_user(size_t count)`, and the linear scan in `PMM::alloc_page_table()`

**ROOT CAUSE:**
Both functions perform a first-fit linear bitmap scan (`for i in 0..total_pages_`, with a nested `for j in 0..count` bit test) to find a free page/run. Worst-case cost scales with `total_pages_` (total physical memory / 4KiB) and fragmentation state, i.e., it is **O(total_pages_ × count)**, not O(1)/bounded. Every kernel and user page allocation funnels through this path. For a hard real-time kernel this makes allocation WCET data-dependent on memory size and fragmentation history — it cannot be certified against a fixed WCET budget.

**REQUIRED FIX:**
1. Replace the bitmap-scan single-page fast path with an O(1) intrusive free-list of page indices: reserve a fixed region of the bitmap-adjacent metadata (or a dedicated static array sized `total_pages_`, allocated once at `PMM::init()`/`init_err()` boot time from the same reserved region used for `bitmap_`/`owner_bitmap_`) to store `next_free_index` links, mirroring the technique already used in `MemPool`.
2. `try_alloc_kernel(1)`/`try_alloc_user(1)` become O(1): pop head of the respective free-list, update bitmap+owner bit for bookkeeping/introspection only (not for allocation decisions).
3. `free_page()`/`free_page_err()` push the freed index back onto the free-list head — O(1).
4. For `alloc_contiguous(count > 1)`, since arbitrary contiguous-run allocation cannot be O(1) with a plain free-list, implement a bounded buddy allocator (power-of-two order classes, `O(log2(total_pages_))` worst case, fixed upper bound at compile time via `constexpr max_order`) — this yields a *provable, documented* WCET bound instead of an open linear scan. Document the bound in a comment directly above the function.
5. `alloc_page_table()`'s pool scan must use the same free-list technique over its bounded `CONFIG_PAGE_TABLE_POOL_SIZE` range (this range is small and fixed, so an O(1) free-list here is trivial and removes the scan entirely).
6. All new metadata structures must be fixed-size, statically placed in the already-reserved physical region computed in `PMM::init()` — no dynamic allocation.

---

## [ ] VULN-004 — No Unconditional Ring-3/Ring-0 Ownership Validation in VMM Page Mapping

**FILE / LOCATION:**
`src/kernel/memory/vmm.cpp` — `VMM::map_page()` (final `pt[pt_idx] = phys_addr | flags;` assignment, both x86_64/aarch64 and RISC-V branches) and `VMM::map_page_in_pml4()` (final `pt[pt_idx] = phys_addr | flags;` / `l2[l2_idx] = ...` assignment)

**ROOT CAUSE:**
The only isolation guard present —
```cpp
if (Scheduler::is_test_active() && pml4_idx >= arch::PML4_USER_COUNT) { ... return; }
```
— is gated behind `Scheduler::is_test_active()` and therefore **inactive in production builds**. Neither `map_page` nor `map_page_in_pml4` cross-checks `phys_addr`'s ownership (`PMM::is_user_page`/`PMM::owner_test`) against the requested `user` flag before writing the leaf PTE. A caller can map a KERNEL-owned physical page (e.g. containing PMM bitmaps, TCBs, or MemPool metadata) into a user page table with `user=true`, granting Ring-3 read/write access to kernel memory. This is an unconditional privilege-isolation gap.

**REQUIRED FIX:**
1. In `VMM::map_page()`, immediately before the final leaf-entry write (all three arch branches: x86_64/aarch64 default path, and the RISC-V `l2[l2_idx] = ...` path), add an unconditional check:
   ```cpp
   if (user) {
       ENSURE(PMM::is_user_page(phys_addr) &&
              "map_page: attempt to map KERNEL-owned physical page as user-accessible");
   }
   ```
2. Apply the identical check in `VMM::map_page_in_pml4()` before its leaf-entry write.
3. This check must run in **all** build configurations, not only when `Scheduler::is_test_active()` — the existing test-only kernel-space-VA rejection in `map_page`/`unmap_page`/`virt_to_phys` should remain as an *additional* defense but must not be the only isolation mechanism.
4. `ENSURE` must be the existing freestanding fail-fast macro (panics, no exceptions, no heap use) already used elsewhere in this file (`kernel/assert.hpp`).
5. No allocation required — this is a pure boolean check against the existing owner bitmap.

---

## [ ] VULN-005 — Non-Atomic Multi-Statement Update of Current-Task Memory Budget Counter

**FILE / LOCATION:**
`src/kernel/memory/pmm.cpp` — `PMM::alloc_page()` and `PMM::alloc_contiguous()`, specifically the pattern:
```cpp
auto *cur = Scheduler::current_task();
if (cur && cur->magic == TaskControlBlock::TCB_MAGIC && cur->memory_used_pages_ >= cur->memory_budget_pages_) { return 0; }
...
uint64_t result = try_alloc_kernel(1);   // <-- preemption / bitmap-scan window
if (result) {
    if (cur && cur->magic == TaskControlBlock::TCB_MAGIC)
        cur->memory_used_pages_ += 1;    // second dereference of stale `cur`
```
(guarded by `#if CONFIG_MEMORY_BUDGET`)

**ROOT CAUSE:**
`cur` is captured once, then dereferenced a second time after an intervening call (`try_alloc_kernel`) that is not currently lock-protected (see VULN-002/003) and may involve scheduling activity. If the current task exits, is descheduled, or its TCB slot is recycled by `MemPool` between the two dereferences, the second `cur->memory_used_pages_ += 1` writes into a stale/reused TCB, corrupting an unrelated task's accounting state (or a freed/recycled block). Re-checking `magic == TCB_MAGIC` reduces but does not eliminate this risk (a recycled TCB can present the same magic).

**REQUIRED FIX:**
1. Do **not** stash `cur` across the allocation call. Re-fetch `Scheduler::current_task()` and re-validate `magic == TCB_MAGIC` at the point of the second update, inside the same lock/IRQ-disabled critical section introduced for VULN-002.
2. Concretely: move the entire sequence — budget check, `try_alloc_kernel(1)`, and budget increment — inside a single `IrqSpinLockGuard` (from VULN-002) so the current task cannot be preempted/torn-down between the check and the increment; within that critical section it is then safe to keep a single `cur` pointer for the duration, since no preemption can occur.
3. Apply the identical pattern to `PMM::alloc_contiguous()`.
4. Do not introduce dynamic allocation or exceptions; use only the existing `Scheduler::current_task()` API and the new spinlock/IRQ-guard primitives.

---

## [ ] VULN-006 — Undocumented, Unbounded WCET in Address-Space Teardown/Clone

**FILE / LOCATION:**
`src/kernel/memory/vmm.cpp` — `VMM::free_user_pages(uint64_t pml4_phys)` and `VMM::deep_copy_user_pages(uint64_t src_pml4, uint64_t dst_pml4)` (all arch branches: x86_64/aarch64 4-level walk, RISC-V Sv39 3/4-level walk)

**ROOT CAUSE:**
Both functions perform full nested traversal of up to 512 entries at each of 3–4 page-table levels (worst case on the order of 512³–512⁴ inner iterations for a fully populated address space), including a `memcpy` of a full 4KiB page per leaf entry in `deep_copy_user_pages`. Unlike `map_page`/`get_table`, which `vmm.hpp` explicitly documents as WCET-bounded, these two functions have **no documented bound, no chunking, and no cooperative yield point**. They are invoked from `exec()`/`fork()`-style hot paths, making process creation/teardown latency data-dependent on how much memory the exiting/forking process had mapped — a hard-RT scheduling hazard (unbounded blocking of higher-priority work behind these calls, since no yield points exist).

**REQUIRED FIX:**
1. Add an explicit, documented WCET bound comment above each function stating the worst-case iteration count as a closed-form expression in terms of `arch::PML4_USER_COUNT` and the fixed 512-entry table width (e.g., `arch::PML4_USER_COUNT * 512 * 512 * 512` leaf visits, each O(1) except the `memcpy`).
2. Convert both functions to **cooperatively yield** at a fixed granularity: after processing every N (e.g., 64) leaf/table entries, call `Scheduler::cleanup_step()` or an explicit `Scheduler::maybe_preempt()`-style checkpoint (whatever bounded-latency yield primitive already exists in `kernel/task/scheduler.hpp`) so a higher-priority ready task can run before the walk continues. This must be implemented as a plain counter increment/modulo check — no allocation, no blocking calls beyond the existing scheduler yield primitive.
3. If no such yield primitive currently exists, add a `static constexpr size_t YIELD_GRANULARITY = 64;` and call the scheduler's existing tick/preemption-check function (whatever is exposed) at that granularity; do not invent new locking — reuse existing scheduler API only.
4. Keep the traversal logic and page-freeing/copying behavior unchanged; only add the periodic yield checkpoint and the WCET-bound documentation.

---

## [ ] VULN-007 — `MemPool::reserve()` and `PMM::pool_used_pages()` Are Unbounded-Relative-to-Config Linear Scans With No Boot-Phase Gate

**FILE / LOCATION:**
- `src/kernel/memory/mempool.cpp` — `MemPool::reserve(size_t pool_idx, size_t count)`
- `src/kernel/memory/pmm.cpp` — `PMM::pool_used_pages()`

**ROOT CAUSE:**
`reserve()` linearly scans `pool.block_count` looking for free blocks to pin; `pool_used_pages()` linearly scans the page-table pool range testing the bitmap. Neither is gated to boot-only use — nothing prevents calling `reserve()` after `ready_ = true` (post-init, on the hot path), at which point its cost becomes an unbudgeted variable-latency operation contending with the pinning/allocation lock introduced in VULN-002.

**REQUIRED FIX:**
1. In `MemPool::reserve()`, add `ENSURE(!ready_ && "MemPool::reserve() must only be called during init(), before ready_ is set");` as the first statement, enforcing boot-phase-only usage at runtime (fail-fast, no behavior change for correct callers).
2. Mark `PMM::pool_used_pages()` and `PMM::pool_total_pages()` in `pmm.hpp` with a doc-comment `@note Diagnostic/introspection only — O(pool_size_pages), must not be called from any WCET-budgeted hot path.` No functional change required; this is a documentation + call-site-audit directive for the fixing agent to grep all call sites and confirm none are in interrupt/scheduler-critical paths.
3. No allocation changes required.

---

## [ ] VULN-008 — Silent No-Op Free on Pinned Blocks; `pin_block()` Does Not Validate Allocation State

**FILE / LOCATION:**
`src/kernel/memory/mempool.cpp` — `MemPool::free()`, `MemPool::free_err()` (the `if (pool.is_block_pinned(block_idx)) return;` / `return MEMPOOL_ERR_OK;` branches) and `MemPool::pin_block()`

**ROOT CAUSE:**
`pin_block()` sets the pinned bit without checking `!pool.is_block_freed(block_idx)` (i.e., without verifying the block is currently allocated, not sitting on the free-list). If pinned while still free, the block can still be dispensed once more by `alloc()` (it remains linked via `first_free`), after which every subsequent `free()` on it becomes a **permanent, silent no-op** that reports success (`MEMPOOL_ERR_OK`) — the block leaks forever with no diagnostic, violating ASIL-D fail-fast/no-silent-error requirements.

**REQUIRED FIX:**
1. In `MemPool::pin_block()`, after computing `block_idx` and before calling `pool.set_block_pinned(block_idx)`, add:
   ```cpp
   ENSURE(!pool.is_block_freed(block_idx) &&
          "pin_block: cannot pin a block that is currently on the free list");
   ```
2. In `mempool_errors.hpp`, add a new error code to the `MEMPOOL_ERROR_CODES` X-macro table, e.g. `X(PINNED, 8, "Free attempted on pinned block")`.
3. In `MemPool::free_err()`, change the pinned-block branch from `return MEMPOOL_ERR_OK;` to `return MEMPOOL_ERR_PINNED;` so callers can distinguish "silently retained" from "successfully freed."
4. In `MemPool::free()` (`void`-returning variant), since it cannot return an error code, add a `Logger::warn("MemPool::free: attempted free of pinned block %zu in pool %zu — ignored", block_idx, i);` call on the pinned branch so the event is at least observable in logs (no allocation, `Logger` is already used elsewhere in this subsystem's call graph via `integrity.cpp`).
5. No dynamic allocation involved in this fix.

---

## [ ] VULN-009 — `PMM::free_page_err()` Omits Owner-Bitmap Reset Present in `PMM::free_page()` (Ownership-Tracking Drift)

**FILE / LOCATION:**
`src/kernel/memory/pmm.cpp` — `PMM::free_page_err(uint64_t phys_addr)`, compare against `PMM::free_page(uint64_t phys_addr)`

**ROOT CAUSE:**
`free_page()` calls `owner_set_kernel(index)` when clearing the allocation bit; `free_page_err()` performs `bitmap_clear(index); ++free_pages_;` but **omits** the `owner_set_kernel(index)` call. A page freed via the `_err` API retains a stale USER-owner bit until it is reallocated and its owner explicitly reset by the next `alloc_user_*`/`alloc_*` call — meaning any `PMM::is_user_page()`/`is_user_page_err()` query issued on that freed-but-not-yet-reallocated page reports incorrect ownership. Given VULN-004's requirement that VMM ownership checks rely on `is_user_page()` being accurate, this drift directly undermines Ring-3/Ring-0 isolation enforcement.

**REQUIRED FIX:**
In `PMM::free_page_err()`, add `owner_set_kernel(index);` immediately after `bitmap_clear(index);`, matching `PMM::free_page()` exactly:
```cpp
if (bitmap_test(index)) {
    bitmap_clear(index);
    owner_set_kernel(index);   // <-- ADD: keep owner-bitmap in sync with free_page()
    ++free_pages_;
    kernel::test::ResourceTracker::instance().track_pmm_free(1);
}
```
No signature or allocation changes required.

---

## [ ] VULN-010 — Idle Loop Expressed as Bounded `for (i < UINT64_MAX)` Misrepresents Non-Terminating Control Flow

**FILE / LOCATION:**
`src/kernel/memory/integrity.cpp` — `kernel::integrity::idle_task_main()`

**ROOT CAUSE:**
```cpp
for (uint64_t _i = 0; _i < UINT64_MAX; ++_i) { ... }
```
This loop never terminates in practice (the idle task never returns) but is syntactically a bounded `for`, which can mislead static WCET/loop-bound analysis tooling into treating it as a finite, boundable construct rather than an explicitly non-terminating kernel primitive.

**REQUIRED FIX:**
Replace with:
```cpp
// [[noreturn]] idle primitive — intentionally non-terminating; excluded from
// per-iteration WCET analysis by design (each loop body iteration below is
// individually WCET-bounded and yields via arch::hlt()).
for (;;) {
    Scheduler::cleanup_step();
    check_section_markers();
    crc_process_chunk();
    arch::hlt();
}
```
No functional change; this is a control-flow clarity fix only, zero allocation impact.

---

## [ ] VULN-011 — Unsynchronized File-Scope CRC/Idle State Assumes Undocumented Single-Writer Invariant

**FILE / LOCATION:**
`src/kernel/memory/integrity.cpp` — file-scope statics `crc_accumulator`, `crc_offset`, `crc_total_len`, `crc_complete`, `crc_checked`; functions `reset_crc_state()`, `crc_process_chunk()`

**ROOT CAUSE:**
These statics are mutated without any lock, relying on an implicit assumption that only the idle task ever calls `reset_crc_state()`/`crc_process_chunk()`. This invariant is not documented or enforced; a future watchdog/IRQ handler or diagnostic path calling `reset_crc_state()` concurrently with the idle task's `crc_process_chunk()` would race on `crc_accumulator`/`crc_offset`.

**REQUIRED FIX:**
1. Add a file-scope `constinit` guard flag, e.g. `static bool crc_owner_lock_ = false;` (or reuse the `SpinLock` primitive introduced in VULN-002 if trivially available), and in both `reset_crc_state()` and `crc_process_chunk()` add:
   ```cpp
   ENSURE(!crc_owner_lock_ && "integrity: concurrent CRC state access detected");
   crc_owner_lock_ = true;
   // ... existing body ...
   crc_owner_lock_ = false;
   ```
   This is a cheap reentrancy assertion, not a blocking lock — it fail-fasts on the single-writer invariant being violated rather than allowing silent data races, matching ASIL-D's fail-fast diagnostic requirement.
2. Add a doc-comment above the static declarations explicitly stating: `// Single-writer invariant: only kernel::integrity::idle_task_main() (and its callees) may touch this state. Enforced at runtime via crc_owner_lock_.`
3. No dynamic allocation; purely a boolean-guarded reentrancy check.
