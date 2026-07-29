# Open Issues

## Kernel — VM / Page Table
- [ ] **pml4_clone test class crashes** — Page Fault after `pml4_fork_no_child_corrupt_parent` and `pml4_free_user_pages_shared_safe` with CR3 corruption (tests 485-486 in all-1). Triggered during snapshot_restore after PML4 clone/fork operations. CR3=0x1000 suggests freed page table. Blocked by HHDM snapshot restore.

# Resolved

### #021 — all-1 GPF at IpcConcurrentSenders (test 80/745)
- **Fix:** `PMM::is_allocated()` safety check in `VMM::get_table()` — before following a PAGE_PRESENT entry, verify the target page is allocated. If not, clear the entry and fall through. Tested: `IpcConcurrentSenders` passes, all-2 passes 133/133, all-1 reaches test 485 without GPF.
- **Committed:** `210feb06` (stale-entry guard + HHDM limit + clear_pte_in_pml4)

### #022 — PCP mutex retry budget exhaustion
- **Root Cause:** Direct ownership transfer in unlock() was missing for the original wake_one()+restore_priority pattern. The lock-stealing race caused PCP retry budget exhaustion.
- **Fix:** Direct ownership transfer in `unlock()`/`unlock_err()` prevents lock stealing. `restore_priority()` ordering fixed (move after waiter removal). 6 test classes migrated to `lock_err()`.
- **Committed:** `52d19137`, `afdf3b84`, `8defb9af`
