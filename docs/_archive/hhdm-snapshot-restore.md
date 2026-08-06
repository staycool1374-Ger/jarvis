# HHDM Snapshot-Restore: Kernel Page-Table Isolation for Test Cycles

**Status:** Spec / Investigation Record  
**Version:** 0.3.6  
**Author:** Investigation session 2026-07-28  
**Related:** `kstack-window-pt-pool.md`, `test_isolate.cpp`, `vmm.cpp`

---

## 1. Problem Statement

The VMM test suite contains three tests that split 2MB huge pages in the kernel HHDM (Higher Half Direct Map) address range:

| Test | Address | Description |
|------|---------|-------------|
| `vmm_huge_page_split_corner` | `0x200000` (user VA) | Huge page split in user space — **safe, re-enabled** |
| `vmm_huge_page_split_regression` | `HHDM_OFFSET + 0x802000` | Huge page split in kernel HHDM — **currently disabled** |
| `vmm_hhdm_access_consistency` | `HHDM_OFFSET + 0x900000` | HHDM access after split/restore — **currently disabled** |

The user-space test is safe because `snapshot_restore` clears all PML4 user entries (0-255) after each test cycle. The kernel HHDM (PML4[256]) is **never restored** — its PD entries persist across test cycles. When a test splits a 2MB huge page, it creates a 4KB page table (PT) allocated from the page-table pool. After `snapshot_restore`, the pool bitmap is restored, freeing the PT page. But the PD entry still points to the freed page → **dangling reference → UAF on next access.**

## 2. Architecture Overview

### 2.1 HHDM Page Table Layout

```
PML4[256] ──→ PDPT (boot, 0-2 MB range)
                │
                ├── PDPT[0] → PD (boot, 0-2 MB range)
                │              ├── PD[0..63] = 2MB huge pages (physical 0–128 MB)
                │              └── PD[64..511] = 0 (zeroed by VMM::init)
                │
                ├── PDPT[63] → PD (pool, for APIC MMIO at 0xFEE00000)
                │              └── [allocated by APIC::map_mmio during boot]
                │
                └── PDPT[1..62, 64..511] = 0 (zeroed by VMM::init)
```

Key points:
- PML4[256] points to the HHDM PDPT, allocated by boot code at a fixed physical address in the 0-2 MB range (always reserved, never freed by PMM restore)
- PDPT[0] points to the HHDM PD page, also in the 0-2 MB range (always reserved)
- PD entries 0-63 are 2MB huge pages set up by boot code, covering physical 0-128 MB
- PD entries 64-511 are zeroed by `VMM::init()`
- Other PDPT entries (for APIC, etc.) are allocated dynamically via `get_table()` during boot and live in the page-table pool

### 2.2 Snapshot-Restore Order (current)

```
1. ResourceTracker check
2. PMM bitmap + owner restore (overwrites entire bitmap from saved state)
3. PtPoolSnapshot overlay (re-marks pool pages from saved state)
4. PML4 user entries clear (0-255)
5. User page content restore
6. MemPool restore
7. Scheduler/Tasks restore
8. BufferPool restore
9. Daemon state restore
10. VFS state restore
```

### 2.3 The Gap

Step 4 only clears PML4[0-255]. PML4[256] (HHDM) is untouched. After a test splits PD[pd_idx] (step 4 in the test flow):

```
Before test:    PD[pd_idx] = 0x8000_00E3  (2MB huge page at physical 0x800000)
After map_page: PD[pd_idx] = PT_page_phys | PRESENT | WRITE | USER  (split)
After restore:  PD[pd_idx] = PT_page_phys | ...  (NOT restored!)
                PT_page: freed by PMM restore (pool bitmap cleared)
```

The PD entry points to a freed pool page. Next access to `HHDM_OFFSET + 0x802000` follows the dangling pointer → UAF.

### 2.4 Pool Relocation (already done in v0.3.6)

The page-table pool was relocated from `reserved_end_page` (~6.8 MB) to the end of the HHDM window (112-128 MB). This ensures `alloc_page()` (scanning from 0) never conflicts with `alloc_page_table()` (scanning the pool range at 112-128 MB). However, the PtPoolSnapshot only protects boot-time pool allocations. Test-allocated pool pages (for HHDM splits) are NOT in the first-snapshot pool bitmap, so they're freed by PMM restore.

## 3. Investigation History

### 3.1 Attempt 1: ScopeGuard in tests (failed)

Added `ScopeGuard` lambdas to cleanup after assertion failures. Problem: the crash happened during `snapshot_restore` AFTER the test passed (at `lapic_wr`), not during test execution. The ScopeGuard was dismissed before test exit, so it wasn't the cause. The crash was from a stale APIC MMIO PT page — the APIC uses PDPT[63]'s PD, which is a pool page allocated during boot. After PtPoolSnapshot restore, the APIC's PD page should be protected. But the crash suggests the order of operations in snapshot_restore corrupts it.

### 3.2 Attempt 2: PD save/restore in snapshot (crashed)

Added `HHDM_PD_BYTES` (4096 bytes) to the snapshot buffer, saved PD content in `snapshot_create`, restored in `snapshot_restore`. The restore ran AFTER `PMM restore + PtPoolSnapshot + PML4 clear`. Crashed with `[FATAL]` infinite loop. Root cause: placing PD restore AFTER PMM restore means `PMM::free_page` inside PD restore tries to free PT pages whose bitmap bits were already cleared by PMM restore. The `free_page` is a no-op (bitmap_test returns false), but the subsequent `__builtin_memcpy(pd, saved, HHDM_PD_BYTES)` writes to the HHDM PD page. If this write is interrupted by an APIC timer interrupt (which fires during restore, since interrupts are disabled but the APIC timer might be pending), the lapic_wr in the EOI handler accesses the APIC MMIO path. The APIC's PD page (PDPT[63]) might share the same physical PD page as PDPT[0]? **No** — they're different physical pages. But the crash still happened, suggesting a deeper issue.

### 3.3 Attempt 3: Remove virt_to_phys guard (caused all-2 crash)

Removed the kernel-space guard from `VMM::virt_to_phys` (line 342 of vmm.cpp). This caused the all-2 test class to crash at test 18 (`vmm_map_already_mapped`). The guard is needed because `virt_to_phys` walks the current CR3's page tables, which might not have kernel-space entries mapped during test execution (since some tests change CR3). Reverting the guard fixed all-2.

### 3.4 Attempt 4: Remove map_page guard + allow HHDM (crashed)

Changed the `map_page` guard from `return` to `warn` (allow but log). This let test 7 proceed, but test 8 crashed during snapshot_restore before running. The crash was at `lapic_wr` — an APIC timer interrupt fired during restore, and the EOI handler crashed because the APIC MMIO path was corrupted.

## 4. The Correct Solution: PD Restore BEFORE PMM Restore

### 4.1 Root Cause of Previous Crash

The crash at `lapic_wr` happened because the PD restore ran AFTER PMM restore. The sequence:

1. PMM restore → overwrites bitmap → frees all test-allocated pool pages
2. PtPoolSnapshot overlay → re-marks boot-time pool pages
3. PML4 user clear → PML4[0-255] = 0
4. **PD restore** → writes saved PD entries, frees PT pages (but PMM already freed them)
5. **APIC timer interrupt fires** → EOI → `lapic_wr` → accesses APIC MMIO at PDPT[63]

The APIC MMIO PT page was allocated during boot (via `APIC::map_mmio` → `get_table` → `alloc_page_table`). It's in the pool. After step 2 (PtPoolSnapshot), it's re-marked as allocated. But between step 2 and step 4, the APIC timer interrupt fires and attempts to use it. The interrupt handler triggers `lapic_wr`, which accesses the APIC MMIO address through the HHDM. The page table walk for `HHDM_OFFSET + 0xFEE00000` goes through:

```
PML4[256] → PDPT (reserved, OK)
PDPT[63] → PD (pool page, should be protected by PtPoolSnapshot)
PD[...] → PT (pool page, should be protected by PtPoolSnapshot)
PT[...] → APIC MMIO page
```

The PtPoolSnapshot restore at step 2 should protect both the PD page (PDPT[63]) and the PT page. But step 4 (PD restore for PDPT[0]) might modify a shared resource or cause a TLB flush that invalidates the APIC's entries temporarily.

**However**, the real issue is simpler: step 4 ran `__builtin_memcpy(pd, saved, HHDM_PD_BYTES)` which writes 4KB to the PD page at PDPT[0]. This PD page is in the 0-2 MB boot page table area. Writing to it requires the write to land on the correct physical page. The HHDM mapping for the PD page should be intact (it's within 0-128 MB). But the memcpy is a 4KB burst write. If an interrupt (APIC timer) fires during this memcpy, the interrupt handler runs while the PD is in a partially-restored state. The APIC timer ISR might access a VA that walks through PDPT[0]'s PD, which is mid-update → sees garbage → crashes.

**The fix:** Run PD restore BEFORE PMM restore, when no partial state exists:

```
1. PD restore (HHDM PDPT[0] → PD) — restore saved entries, free split PT pages
2. PMM bitmap + owner restore
3. PtPoolSnapshot overlay
4. PML4 user entries clear
5. ... rest of restore ...
```

By running PD restore first, the PT pages are freed into the current live bitmap (not the restored one). Then PMM restore at step 2 overwrites the bitmap with the saved state, which includes the freed PT pages as... whatever they were before the test (probably free, since they were test-allocated). This is correct: the PT pages were allocated during the test, freed by PD restore, and PMM restore confirms they're free. No double-free.

### 4.2 Detailed Step-by-Step

**snapshot_create additions:**
```
After PtPoolSnapshot capture:
1. Read PML4 via VMM::get_kernel_pml4()
2. Walk: PML4[256] → PDPT → PDPT[0] → PD
3. Copy PD[0..511] (4096 bytes) to g_snapshot + off_hhdm_pd(...)
```

**snapshot_restore additions (at beginning, before PMM restore):**
```
1. Read saved PD entries from g_snapshot + off_hhdm_pd(...)
2. Walk current PML4 to find current PD
3. For each i in 0..511:
   a. saved[i] is a 2MB huge page AND pd[i] is NOT a huge page AND pd[i] is present
      → pd[i] points to a PT page that was inserted by test → free it via PMM::free_page
4. memcpy saved PD entries into current PD (restore all entries)
```

**Snap buffer layout change:**
Insert `off_hhdm_pd` right before `off_pt_pool` in the snapshot layout. This adds 4096 bytes (one additional page if the buffer was page-aligned before). Total snapshot size increase: 4096 bytes.

### 4.3 Integration with Existing Fixes

| Fix | Status | Impact |
|-----|--------|--------|
| PtPoolSnapshot bitmap fix | ✅ Committed | Prevents pool UAF from PtPoolSnapshot size bug |
| Pool relocation to end of HHDM | ✅ Committed | Prevents `alloc_page` from using pool pages |
| alloc_page_table no-fallback | ✅ Committed | All PT pages come from pool, protected by PtPoolSnapshot |
| virt_to_phys guard kept | ✅ Confirmed | Needed for all-2 stability; HHDM tests don't need it removed |
| map_page guard → warn | ❌ NOT safe with current state | Must stay as `return` until PD restore is implemented |

### 4.4 Map Page Guard

The `map_page` kernel-space guard at vmm.cpp:256 MUST remain as `return` (blocking) until the PD restore is properly integrated. Once PD restore is proven stable, the guard can be changed to `warn` (allow but log) to let HHDM tests proceed. The PD restore will clean up any modifications.

## 5. Edge Cases and Failure Modes

### 5.1 APIC Timer Interrupt During PD Restore

**Risk:** The APIC timer fires during `__builtin_memcpy(pd, saved, 4096)`, which is a 4KB burst write. The interrupt handler might access a VA that walks through the same PD being modified.

**Mitigation:** The PD restore runs with `IrqGuard` active (it's called within `snapshot_restore` which already disables IRQs via `arch::IrqGuard guard` at the top). However, pending interrupts might trigger after the guard is released. The fix: run PD restore with IRQs disabled, and ensure no TLB flush or cache operation occurs between the memcpy and the point where interrupts are re-enabled.

**Verification:** Check that the APIC timer interrupt is NOT pending-by-default at restore time. The snapshot_restore already resets the APIC timer state via the scheduler misc data (misc[9] = timer_ticks).

### 5.2 Multiple PDPT Entries Modified

**Risk:** Tests could modify PD pages at PDPT entries other than PDPT[0] (e.g., the APIC at PDPT[63]).

**Mitigation:** Currently, no test modifies PDPT[63]'s PD. All HHDM tests use addresses within the first 1GB (PDPT[0]). If future tests use other PDPT entries, the PD restore must be extended to save/restore those PDs too. For v0.3.6, only PDPT[0] is covered.

### 5.3 Double-Free of PT Pages

**Risk:** PD restore frees a PT page (via `PMM::free_page`). Then PMM restore (step 2) overwrites the bitmap, potentially double-freeing the same page.

**Analysis:** The PT page was allocated from the pool during the test. The saved PMM bitmap (from before the test) has the page as free (since it wasn't allocated at boot time). PD restore frees it (sets bitmap bit to 0). PMM restore then writes the saved bitmap (which also has the bit as 0). Result: bit stays 0 → no double-free. ✓

### 5.4 PT Page in Wrong Pool Range

**Risk:** `alloc_page_table()` for the HHDM split returns a page from the pool. After PD restore + PMM restore + PtPoolSnapshot, the pool bitmap remembers the first-snapshot state (boot-time allocations). The PT page was NOT a boot-time allocation, so it's NOT re-marked by PtPoolSnapshot. PD restore freed it, PMM restore confirms it's free → correct.

But what if `alloc_page_table()` for the HHDM split falls back to `alloc_page()` (pool exhausted)? With the **no-fallback** change (committed), this panics the kernel. So pool exhaustion must not happen. 4096 pool pages with ~200 consumed at boot leaves 3896 for test usage. Each test uses 1-5 PT pages → more than enough for 850+ tests when PtPoolSnapshot frees them between tests.

### 5.5 Split at PD Boundary

**Risk:** The HHDM split tests use addresses at 0x802000 and 0x900000, both within PD entries 4 and 4 respectively (covers 8-10 MB). If a test splits at a PD boundary (e.g., 0x1000000 = 16 MB, which is PD entry 8), the PT page overlaps two 2MB regions.

**Mitigation:** The `get_table` function handles this correctly — it creates a single PT page covering 512 entries × 4KB = 2MB, regardless of where within the 2MB region the target VA falls. The PT page maps all 512 entries to the huge page's constituent physical pages.

### 5.6 PD Save Stale After APIC MMIO

**Risk:** The first snapshot captures the PD at PDPT[0] after boot, including the APIC's mapping at PDPT[63] (which is a DIFFERENT PD page — safe). But what if the APIC or another subsystem modifies PDPT[0]'s PD entries after the first snapshot but before the HHDM test runs?

**Reality:** Nothing modifies PDPT[0]'s PD entries between snapshot creation and test execution. The boot code sets them once, `VMM::init` doesn't touch them, and `APIC::map_mmio` uses PDPT[63], not PDPT[0]. The only modifications to PDPT[0]'s PD come from HHDM tests or from boot-time code that runs before the first snapshot (e.g., `VMM::map_page` for APIC at HHDM_OFFSET + 0xFEE00000 — but that's at PDPT[63], not PDPT[0]).

## 6. Implementation Plan

### Step 1: Add off_hhdm_pd to snapshot layout (test_isolate.cpp)

```cpp
static constexpr size_t HHDM_PD_BYTES = 512 * sizeof(uint64_t); // 4096

static size_t off_hhdm_pd(size_t user_page_count, uint64_t num_kstacks) {
    size_t kstack_area = sizeof(uint64_t) + num_kstacks * KSTACK_ENTRY_SIZE;
    return off_kstack_header(user_page_count) + kstack_area;
}

// Modify off_pt_pool to account for hhdm_pd
static size_t off_pt_pool(size_t user_page_count, uint64_t num_kstacks) {
    return off_hhdm_pd(user_page_count, num_kstacks) + HHDM_PD_BYTES;
}
```

### Step 2: Save PD in snapshot_create (test_isolate.cpp)

After the PtPoolSnapshot capture block, add:
- Walk kernel PML4 → PDPT[0] → PD
- memcpy PD content to `g_snapshot + off_hhdm_pd(...)`

### Step 3: Restore PD at beginning of snapshot_restore (test_isolate.cpp)

BEFORE the PMM bitmap restore block:
- Walk kernel PML4 → PDPT[0] → PD
- For each saved entry that was a 2MB huge page:
  - If current entry is not a huge page and is present → free the PT page
- memcpy saved entries to current PD

### Step 4: Re-enable HHDM tests (test_vmm.cpp)

Remove `#if 0` from regression and hhdm_access tests. Uncomment registrations.

### Step 5: Change map_page guard to allow HHDM (vmm.cpp)

Change from `return` to `Logger::warn` (allow but log). The PD restore in snapshot_restore handles cleanup.

### Step 6: Verify

- Run `make execute-test x86_64 debug vmm` → 10/10 PASS (3 new tests pass)
- Run `make execute-test x86_64 debug all-2` → 133/133 PASS
- Run `make execute-test x86_64 debug selftest` → 132/132 PASS
- Run `make execute-test x86_64 debug all-1` (745 tests) → verify no cumulative corruption

## 7. Diagrams

### 7.1 Page Table Walk for HHDM Address

```
VA: 0xFFFF8000_0080_2000
    │
    ▼ bits 47:39 (PML4)
    0x100 = 256  ───→ PML4[256] = 0x3003
    │
    ▼ bits 38:30 (PDPT)
    0              ───→ PDPT[0] = 0x5003
    │
    ▼ bits 29:21 (PD)
    4              ───→ PD[4] = 0x8000_00E3  (2MB huge page)
    │                           │
    ▼                          └─ physical base = 0x800000
    bits 20:0                          + offset = 0x2000
    0x2000                     ───────→ 0x802000
```

### 7.2 Before/After Split

```
Before test:     PD[4] = 0x8000_00E3 (2MB huge page, phys 0x800000)
After map_page:  PD[4] = 0xABCD_0003 (PT page at phys 0xABCD000)
                 PT[0x2000>>12] = 0xTEST_PHYS | PRESENT | WRITE
After restore:   PD[4] = 0x8000_00E3 (restored to huge page)
                 PT page freed via PMM::free_page(0xABCD000)
```

### 7.3 Snapshot Restore Order (corrected)

```
1.  [NEW] PD restore: free split PT pages, restore PD entries
2.  PMM bitmap + owner restore
3.  PtPoolSnapshot overlay
4.  PML4 user entries clear
5.  User page content restore
6.  MemPool restore
7.  Scheduler/Tasks restore
8.  BufferPool restore
9.  Daemon state restore
10. VFS state restore
```

---

## 8. Open Questions

1. **PDPT[0] vs PDPT[63]:** The APIC MMIO uses PDPT[63], not PDPT[0]. These are different PD pages. But do they share a physical page? The boot code sets up PDPT[0] → PD (for identity map of first 1GB). PDPT[63] is zeroed by `VMM::init()` and allocated later by `get_table` during `APIC::map_mmio`. They are different physical pages. ✓

2. **TLB invalidation:** After restoring the PD entries, does the TLB need to be flushed? The restored PD entries are the same as the boot-time 2MB huge pages. If a test split them, the TLB might have cached the split entries. After restore, the TLB has stale entries pointing to the freed PT pages. **Fix:** Add `invlpg` or CR3 reload after PD restore.

3. **Snapshot buffer fragmentation:** Adding 4096 bytes to the snapshot buffer might push the total past a page boundary, requiring an extra page allocation. This is safe — `alloc_contiguous` handles it. The extra page is freed when `snapshot_destroy` runs.

---

## 9. Related Documents

- `docs/kstack-window-pt-pool.md` — Original PtPoolSnapshot design spec
- `src/kernel/test/test_isolate.cpp` — Snapshot save/restore implementation
- `src/kernel/memory/vmm.cpp` — VMM page table manipulation
- `src/kernel/test/test_vmm.cpp` — VMM test suite with HHDM tests
