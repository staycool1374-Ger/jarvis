# Security Audit Report — `kernel::MemPool`
**Component:** `src/kernel/memory/mempool.{hpp,cpp}`
**Reviewer:** Lead Kernel Architect, Jarvis RTOS
**Disposition:** Attacker findings triaged against source. 3 of 4 substantive findings verified as genuine defects. Fixes below are minimal, zero-allocation, and preserve O(1) alloc/free semantics.

---

## Triage Summary

| # | Attacker Claim | Verdict | Rationale |
|---|---|---|---|
| 1 | No locking on shared `pools_[]` state | ✅ **CONFIRMED — CRITICAL** | Verified: zero synchronization primitives in `alloc`/`alloc_err`/`free`/`free_err`. Real double-allocation / free-list-corruption race. |
| 2 | Unbounded snapshot buffer pointers | ⚠️ **CONFIRMED — MEDIUM (downgraded)** | Real gap (no length parameter), but this is a *test-isolation-only* API, not attacker-reachable from untrusted input. Still fixed per defense-in-depth. |
| 3 | `size_t*` vs `uint64_t*` free-list width mismatch | ✅ **CONFIRMED — HIGH** | Verified: `init/alloc/free` use `size_t*`, `restore_pool_meta` uses `uint64_t*` for the same embedded link field. Real latent corruption on any ABI where `sizeof(size_t) != 8`. |
| 4 | WCET bounded loops | ✅ Correctly marked compliant | No action needed. |
| 5 | UB-intrinsic guarding N/A | ✅ Correctly marked N/A | No action needed. |
| 6 | "No IrqGuard either" | ❌ **FALSE / DUPLICATE** | This is not an independent finding — it's restating Finding 1. Absence of a forbidden primitive is not itself a defect. Folded into Finding 1's fix. |
| — | Implicit claim that `contains()` needs locking | ❌ **FALSE POSITIVE** | `contains()` only reads `data`, `block_size`, `block_count`, `initialized` — all of which are write-once during single-threaded `init()` and never mutated afterward. No lock is needed there; only the free-list mutation path (`first_free`, `free_count`, bitmap) is a race target. |

**Verdict: 3 verified defects, fixes mandatory before merge. `contains()` and Finding 6 rejected as false positives.**

---

## 🔴 FINDING 1 (CRITICAL) — Missing per-pool critical section on free-list mutation

### Analysis
`Pool::first_free`, `Pool::free_count`, and the embedded freed-bitmap are mutated via non-atomic read-modify-write sequences in `alloc()`, `alloc_err()`, `free()`, and `free_err()`, with **no lock of any kind**. Two concurrent callers (SMP core + core, or task + ISR-deferred free) can:

- Both read the same `pool.first_free`, both pop the **same block index**, and receive the same pointer as two distinct allocations.
- Interleave `set_block_freed`/`clear_block_freed` bit-flips into a corrupted bitmap.
- Defeat the `ENSURE(!pool.is_block_freed(...))` double-free guard via TOCTOU, since the check and the mutation are two separate non-atomic steps.

Fix: a **per-`Pool` `SpinLock`**, scoped tightly around the free-list mutation, per the fine-grained locking mandate. `find_pool()` and the pointer-range scan in `free()` operate only on write-once-at-init fields and remain lock-free for speed; the lock is acquired only once ownership/index is resolved.

### Fix — `mempool.hpp`

```diff
 #pragma once

 #include <types.hpp>
 #include <kernel/memory/mempool_errors.hpp>
+#include <kernel/sync/spinlock.hpp>

 namespace kernel {
@@
     struct Pool {
         size_t  block_size;
         size_t  block_count;
         size_t  free_count;
         uint8_t* data;
         size_t  first_free;
         bool    initialized;
+        /// @brief Guards first_free / free_count / freed_bitmap mutation.
+        /// @note Fine-grained: one lock per pool class, not a global lock.
+        SpinLock lock;

         Pool()
             : block_size(0)
             , block_count(0)
             , free_count(0)
             , data(nullptr)
             , first_free(0)
             , initialized(false)
+            , lock()
             {}
```

### Fix — `mempool.cpp`

```diff
 void* MemPool::alloc(size_t size) {
     size_t idx = find_pool(size);
     if (idx >= POOL_COUNT) return nullptr;

     auto& pool = pools_[idx];
+    SpinLockGuard guard(pool.lock);
+
     if (pool.free_count == 0) return nullptr;

     size_t block = pool.first_free;
     ENSURE(block < pool.block_count);
```

```diff
 void MemPool::free(void* block) {
     if (!block) return;

     for (size_t i = 0; i < POOL_COUNT; ++i) {
         auto& pool = pools_[i];
         if (!pool.initialized) continue;

         uint8_t* start = pool.data;
         uint8_t* end = pool.data + pool.block_size * pool.block_count;
         uint8_t* p = static_cast<uint8_t*>(block);

         if (p >= start && p < end) {
+            SpinLockGuard guard(pool.lock);
             size_t offset = static_cast<size_t>(p - start);
             size_t block_idx = offset / pool.block_size;
             ENSURE(offset % pool.block_size == 0);
             ENSURE(block_idx < pool.block_count);
             ENSURE(!pool.is_block_freed(block_idx)
                    && "double-free detected");
```

Apply the **identical** two-line guard insertion to `alloc_err()` and `free_err()` (byte-identical logic bodies):

```diff
     auto& pool = pools_[idx];
+    SpinLockGuard guard(pool.lock);
+
     if (pool.free_count == 0) {
         return MEMPOOL_ERR_OOM;
     }
```

```diff
         if (p >= start && p < end) {
+            SpinLockGuard guard(pool.lock);
             size_t offset = static_cast<size_t>(p - start);
```

> **Note on `contains()`:** intentionally left unlocked — it only inspects fields fixed at `init()` time and never mutated afterward. Locking it would add overhead with zero correctness benefit.

---

## 🟠 FINDING 3 (HIGH) — Free-list link width inconsistency (`size_t*` vs `uint64_t*`)

### Analysis
The embedded intrusive-list "next" pointer is written with **two different widths** depending on code path:

- `init()`, `alloc()`, `free()`, `alloc_err()`, `free_err()`: `reinterpret_cast<size_t*>(...)`
- `restore_pool_meta()`: `reinterpret_cast<uint64_t*>(...)`

On any target where `sizeof(size_t) != 8` (32-bit build configs are explicitly in-scope for this RTOS's portability surface), the two code paths disagree on how many bytes of the block they read/write for the same logical field — a real, silent corruption vector for the smallest (16-byte) block class, and a violation of strict-aliasing hygiene besides. This must be unified to a single explicitly-sized type used at **every** touch point.

### Fix — `mempool.cpp`

Introduce one canonical link type and use it everywhere the free-list pointer is read or written:

```diff
 namespace kernel {

 MemPool::Pool MemPool::pools_[POOL_COUNT] = {};
 bool MemPool::ready_ = false;
+
+// Canonical on-disk width of the embedded free-list "next" link.
+// Every touch point (init/alloc/free/restore) MUST use this type —
+// never size_t directly — so the representation is independent of
+// the host size_t width.
+using FreeLink = uint64_t;
+static_assert(sizeof(FreeLink) <= 16,
+              "FreeLink must fit within the smallest pool block (16 bytes)");
```

```diff
         pool.first_free = 0;
         for (size_t j = 0; j < pool.block_count; ++j) {
-            size_t* next = reinterpret_cast<size_t*>(
+            FreeLink* next = reinterpret_cast<FreeLink*>(
                 pool.data + j * pool.block_size);
             *next = j + 1;
             pool.set_block_freed(j);
         }
         {
-            size_t* next = reinterpret_cast<size_t*>(pool.data + (
+            FreeLink* next = reinterpret_cast<FreeLink*>(pool.data + (
                 pool.block_count - 1) * pool.block_size);
-            *next = static_cast<size_t>(-1);
+            *next = static_cast<FreeLink>(-1);
         }
```

```diff
     size_t block = pool.first_free;
     ENSURE(block < pool.block_count);
     ENSURE(pool.is_block_freed(block)
            && "free-list corruption: alloc of already-allocated block");
-    size_t* next = reinterpret_cast<size_t*>(pool.data + block * pool.block_size
-        );
-    pool.first_free = *next;
+    FreeLink* next = reinterpret_cast<FreeLink*>(
+        pool.data + block * pool.block_size);
+    pool.first_free = static_cast<size_t>(*next);
     --pool.free_count;
     pool.clear_block_freed(block);
```
*(apply the same 3-line change identically inside `alloc_err()`)*

```diff
             pool.set_block_freed(block_idx);
-            size_t* next = reinterpret_cast<size_t*>(p);
-            *next = pool.first_free;
+            FreeLink* next = reinterpret_cast<FreeLink*>(p);
+            *next = static_cast<FreeLink>(pool.first_free);
             pool.first_free = block_idx;
```
*(apply the same change identically inside `free_err()`)*

```diff
     p.first_free  = static_cast<size_t>(-1);
     p.free_count  = 0;
     for (size_t j = 0; j < p.block_count; ++j) {
         if (!p.is_block_freed(j)) continue;
-        auto* next = reinterpret_cast<uint64_t*>(
+        FreeLink* next = reinterpret_cast<FreeLink*>(
             p.data + j * p.block_size);
-        *next = p.first_free;
+        *next = static_cast<FreeLink>(p.first_free);
         p.first_free = j;
         ++p.free_count;
     }
```

Every free-list touch point now shares one representation (`FreeLink = uint64_t`), regardless of host `size_t` width — eliminating the cross-path corruption vector.

---

## 🟡 FINDING 2 (MEDIUM) — Snapshot API accepts unbounded raw pointers

### Analysis
`capture_pool_data(uint8_t* dst)` / `restore_pool_data(const uint8_t* src)` advance `dst`/`src` by `pool_data_bytes()` worth of memcpy'd data with no way for the callee to verify the caller's buffer is actually that large. This is test-isolation-only code (not reachable from untrusted/attacker input), so it does **not** rise to the same severity as Finding 1/3 — but it should still fail loudly rather than silently overrun on a caller mismatch.

### Fix — `mempool.hpp`

```diff
-    static void capture_pool_data(uint8_t* dst);
+    static void capture_pool_data(uint8_t* dst, size_t dst_len);
     /// @brief Overwrite all pool block-data from @p src.
-    static void restore_pool_data(const uint8_t* src);
+    static void restore_pool_data(const uint8_t* src, size_t src_len);
```

### Fix — `mempool.cpp`

```diff
-void MemPool::capture_pool_data(uint8_t* dst) {
+void MemPool::capture_pool_data(uint8_t* dst, size_t dst_len) {
+    ENSURE(dst_len == pool_data_bytes()
+           && "capture_pool_data: destination buffer size mismatch");
     for (size_t i = 0; i < POOL_COUNT; ++i) {
         auto& p = pools_[i];
         if (!p.initialized || !p.data) continue;
         size_t bytes = p.block_count * p.block_size;
         __builtin_memcpy(dst, p.data, bytes);
         dst += bytes;
     }
 }

-void MemPool::restore_pool_data(const uint8_t* src) {
+void MemPool::restore_pool_data(const uint8_t* src, size_t src_len) {
+    ENSURE(src_len == pool_data_bytes()
+           && "restore_pool_data: source buffer size mismatch");
     for (size_t i = 0; i < POOL_COUNT; ++i) {
         auto& p = pools_[i];
         if (!p.initialized || !p.data) continue;
         size_t bytes = p.block_count * p.block_size;
         __builtin_memcpy(p.data, src, bytes);
         src += bytes;
     }
 }
```

> Callers of `capture_pool_data`/`restore_pool_data` in test harnesses must be updated to pass the buffer's actual length (typically `pool_data_bytes()`).

---

## Rejected Findings

- **Finding 6** — not an independent defect; restates Finding 1 under a different label. No separate fix required beyond the Finding 1 patch.
- **Implicit `contains()` locking claim** — false positive; `contains()` touches only immutable-post-init fields (`data`, `block_size`, `block_count`, `initialized`) and requires no lock.
- **Findings 4 & 5** — correctly assessed by the attacker as compliant; no code change.

---

## Final Verdict

**BLOCK MERGE until Findings 1, 2, and 3 are patched as above.** Finding 1 (no locking on shared free-list state) is disqualifying on its own for a multi-core-reachable allocator. Findings 2 and 3 are real but lower-severity hardening/portability fixes bundled into the same patch set for a single review cycle.