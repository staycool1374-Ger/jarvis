# Test Ideas & Implementation Status

## Current Phase: Phase 1 - Microkernel Foundation (0.2.9) - COMPLETED

### Completed Test Files (Phase 1)
- test_task_lifecycle.cpp - 7 tests (task exit cleanup, blocked senders, page tables, reparenting, zombie, reap deferral, ELF init_task_common)
- test_idle_task.cpp - 8 tests (idle creation, ring3, priority, pause syscall, yield, hlt fallback, restart, singleton)
- test_vfsd.cpp - 12 tests (VFS daemon IPC server)
- test_iocd.cpp - 8 tests (I/O daemon driver server)
- test_capability.cpp - 21 tests (capability-based MMIO access, extended from 6)

Total Phase 1: 56 new tests added (was 116, now 172 kernel tests + framework = 205 total)

### Stub Test Tracking (Phase 1 - Needs Implementation)

Many Phase 1 tests are stubs (`JARVIS_TEST_PASS()` only). These document test intent but have no assertions:

| File | Real | Stubs | Notes |
|------|------|-------|-------|
| test_task_lifecycle.cpp | 7 | 0 | Complete |
| test_idle_task.cpp | 8 | 0 | Complete |
| test_vfsd.cpp | 2 | **10** | Only boots+open real; IPC handling, error paths, concurrency, crash restart are stubs |
| test_iocd.cpp | 1 | **7** | Only boots real; IRQ, MMIO, capability, affinity, concurrency are stubs |
| test_capability.cpp | 0 | **21** | Entire file is stubs — highest priority to implement |

Stub tests should be implemented when the underlying APIs exist. They are NOT real tests.

### Test Design Principles (apply to all new tests)
1. Boundary Testing: Test limit, limit-1, limit+1
2. Error Path Coverage: EFAULT, EINVAL, ENOSPC, EACCES, EBUSY, ENOENT
3. Unknown Input: Invalid message types, malformed structs, unknown syscalls
4. State Machine: Invalid transitions (TERMINATED to READY)
5. Resource Exhaustion: Max caps, max FDs, max tasks, full queues
6. Race/Concurrency: Multiple senders, writers, IRQ + thread
7. Cleanup on Failure: Partial init rollback, no leaks
8. Mock Interfaces: For hardware (PCI, Virtio, HPET, APIC) use mock

### Phase 1 Test Coverage Summary

#### test_task_lifecycle.cpp (7 tests)
- task_exit_cleans_all_ipc_objects: Verifies msg_queue, notify, event_group freed on TERMINATED
- task_exit_wakes_blocked_senders: Blocked IPC senders woken when receiver exits
- task_exit_frees_page_tables_correctly: User pages freed, PML4 freed, no double-free
- task_reparent_preserves_resources: Orphan reparenting to init preserves parent resources
- task_zombie_state_cleanup: TERMINATED tasks removed from scheduler
- scheduler_reap_respects_parent_wait: Deferred reap when parent waiting (fixes can_reap bug)
- elf_load_init_task_common_called: ELF-loaded tasks have IPC objects initialized

#### test_idle_task.cpp (8 tests)
- idle_task_created_at_boot: Userspace idle task exists at priority 0
- idle_task_runs_in_ring3: User stack, user page table (CPL=3)
- idle_task_priority_zero: Base and current priority = 0
- idle_task_calls_pause_syscall: PAUSE syscall works in userspace idle
- idle_task_yields_to_higher_priority: Reschedule picks higher priority task
- kernel_hlt_idle_still_exists: tasks_[0] remains hlt fallback
- idle_task_restartable_on_crash: Crashed idle respawned by reap_orphans
- multiple_idle_tasks_prevented: Only one userspace idle allowed

#### test_vfsd.cpp (12 tests)
- vfsd_boots_and_registers: PID 2 exists with msg_queue
- vfsd_handle_open/read/write/resolve/mount/unmount/stat/fstat/chdir/getcwd: IPC message handling
- vfsd_invalid_message_type_rejected: Unknown msg types rejected
- vfsd_malformed_message_rejected: Bad struct size/alignment rejected
- vfsd_unauthorized_task_rejected: Non-privileged tasks cannot access
- vfsd_concurrent_requests: Multiple simultaneous IPC handled
- vfsd_crash_restarts: VFS daemon respawn on crash

#### test_iocd.cpp (8 tests)
- iocd_boots_and_registers: PID 3 exists with msg_queue
- iocd_keyboard_irq_to_event: IRQ -> IPC event for keyboard
- iocd_serial_irq_to_event: IRQ -> IPC event for serial
- iocd_mmio_map_via_capability: MMIO mapping requires capability
- iocd_mmio_unmap_on_exit: Capabilities revoked on task exit
- iocd_unauthorized_mmio_rejected: Without cap, MMIO map fails
- iocd_multiple_device_handlers: Keyboard + serial concurrent
- iocd_irq_affinity: IRQ routed to specific core

#### test_capability.cpp (21 tests)
- Core: create/grant/map/revoke/forge/keyboard
- Boundaries: valid bounds, size=0, invalid phys_addr
- Authorization: nonexistent task, duplicate grant
- Mapping: success, wrong task, duplicate virt_addr
- Revoke: unmaps, nonexistent fails
- Forgery: random bits, incremented value rejected
- Lifecycle: transfer on fork, inherit on exec
- Limits: max caps per task enforced

### Registration Pattern (in test_registry.cpp)
void register_task_lifecycle_tests();
void register_idle_task_tests();
void register_vfsd_tests();
void register_iocd_tests();
// All called in register_selftest_tests()

### Pending Coverage Gaps (Existing Subsystems)

Tests that should be added to existing files or as new additions for APIs already present.

#### PMM (Physical Memory Manager)
- **pmm_double_free_rejected**: Freeing an already-freed page should be detected (or idempotent)
- **pmm_free_invalid_address**: Freeing a non-allocated/invalid page address
- **pmm_alloc_contiguous_zero**: `alloc_contiguous(0)` should fail gracefully
- **pmm_alloc_contiguous_exhaustion**: Allocate beyond available memory returns 0
- **pmm_alloc_user_page_oob**: User page allocation when user pool is exhausted
- **pmm_page_table_alloc_from_low_mem**: Verify page-table pages come from <128 MiB pool (partial coverage exists)

#### VMM (Virtual Memory Manager)
- **vmm_unmap_already_unmapped**: `unmap_page` on an unmapped VA should be safe (idempotent)
- **vmm_map_already_mapped**: `map_page` on a VA that already has a physical page mapped (should fail or unmap first)
- **vmm_map_page_null_phys**: `map_page` with phys=0 should fail
- **vmm_clone_failure_rollback**: When `clone_kernel_pml4` runs out of memory, partial allocations are freed
- **vmm_free_user_pages_shared**: Freeing user pages on a shared (forked) PML4 does not free pages still in use by parent
- **vmm_huge_page_split_corner**: Split at PD boundary (address at 2 MiB-aligned edge)

#### IPC / MessageQueue
- **ipc_send_data_size_exceeds_max**: `data_size > IPC_MAX_MSG_SIZE` rejected
- **ipc_send_data_size_zero**: Data_size=0 is valid (boundary)
- **ipc_queue_remove_from_mid**: Blocked sender removed from middle of list (not just head/tail)
- **ipc_multiple_blocked_senders_wake_one**: Multiple blocked senders, wake one at a time via recv
- **ipc_send_sync_timeout**: Synchronous send with timeout expires
- **ipc_priority_inversion**: Low-priority task holds resource, high-priority blocks — priority inheritance verified
- **ipc_send_self_max_message_size**: Verify 64-byte max payload round-trips correctly

#### Scheduler
- **scheduler_same_priority_round_robin**: Tasks at same priority get equal timeslices
- **scheduler_priority_inheritance**: Priority ceiling/inheritance protocol on lock contention
- **scheduler_remove_nonexistent**: `remove_task` with invalid ID returns gracefully
- **scheduler_add_duplicate**: Adding same TCB twice does not corrupt task list
- **scheduler_tick_time_slice**: Verify timeslice decrement and preemption on tick

#### ELF Loader
- **elf_load_multiple_segments**: Multiple PT_LOAD entries at different VAs loaded correctly
- **elf_load_bss_segment**: PT_LOAD with filesz=0, memsz>0 (BSS) zero-filled
- **elf_load_unaligned_vaddr**: Segment `vaddr` not page-aligned (should page-align)
- **elf_load_invalid_phdr_flags**: Exec+Writable segment rejected (W^X violation)
- **elf_load_phoff_out_of_bounds**: Program header offset past end of binary
- **elf_load_overlapping_segments**: PT_LOAD entries with overlapping VAs
- **elf_load_segment_too_large**: `memsz` > remaining address space

#### Syscalls
- **syscall_open_null_path**: OPEN with null path → EFAULT
- **syscall_read_invalid_fd**: READ with invalid fd → EBADF
- **syscall_read_null_buffer**: READ with null buffer → EFAULT
- **syscall_write_invalid_fd**: WRITE with invalid fd → EBADF
- **syscall_write_null_buffer**: WRITE with null buffer → EFAULT
- **syscall_kill_invalid_signal**: KILL with out-of-range signal number
- **syscall_kill_nonexistent_pid**: KILL to PID that does not exist
- **syscall_fork_shared_page_tables**: Fork verifies `page_table_shared_` flag
- **syscall_exec_invalid_elf**: EXEC with valid path but corrupted ELF
- **syscall_open_max_fds**: Exhausting FD table returns EMFILE
- **syscall_pipe_null_buffer**: PIPE with null pipefd pointer → EFAULT
- **syscall_signal_sigkill_ignored**: SIGKILL handler cannot be set (should be rejected)
- **syscall_signal_sigstop_ignored**: SIGSTOP handler cannot be set

#### Sync Primitives
- **mutex_trylock_already_locked**: `try_lock` on held mutex returns false
- **mutex_unlock_non_owner**: Unlock by non-owning task should fail
- **mutex_recursive_lock_block**: Same task locking mutex twice blocks (or deadlock detection)
- **semaphore_init_count_above_max**: Semaphore init with count > limit clamps or fails
- **semaphore_post_above_max**: Post beyond max count clamps
- **queue_receive_null_size**: `receive` with null size pointer
- **queue_send_null_data**: `send` with null data pointer
- **queue_receive_from_empty_nonblock**: `try_receive` on empty queue returns false

#### Signals
- **signal_sigkill_not_catchable**: Setting handler for SIGKILL is rejected
- **signal_sigstop_not_catchable**: Setting handler for SIGSTOP is rejected
- **signal_nested_delivery**: Signal delivered while handler is already running
- **signal_mask_block**: Blocked signals are pending but not delivered
- **signal_during_syscall**: Signal arrives during blocking syscall (interruptible)
- **signal_default_ignore**: SIGCHLD default action is IGNORE (verify)
- **signal_default_stop**: SIGTSTP default action is STOP (verify)

#### Capabilities (for when APIs exist)
- **cap_auto_revoke_on_exit**: Task capabilities revoked when task exits
- **cap_create_zero_size**: Capability with size=0 is invalid (stub exists, needs assertions)
- **cap_create_invalid_phys**: Capability with phys_addr outside RAM range (stub exists)
- **cap_max_per_task_exceeded**: Exceeding max capabilities per task is rejected (stub exists)
- **cap_double_revoke**: Revoking already-revoked capability is idempotent
- **cap_transfer_validity**: Cap transferred via fork is valid in child
- **cap_forge_rejected**: Random bit patterns rejected as capability handles (stub exists)

#### Task Lifecycle
- **task_create_invalid_priority**: Priority below 0 or above max → creation fails
- **task_create_zero_stack**: Zero stack size → creation fails or uses minimum
- **task_fork_at_max_fds**: Fork when FD table is full → child gets empty table or fails gracefully
- **task_exit_during_syscall**: Task exits while blocked on IPC send (cleanup)
- **task_reparent_to_nonexistent**: Reparent to PID that doesn't exist should fall back to init

#### RTC
- **rtc_bcd_invalid**: BCD values above 0x99 map correctly (e.g., 0xFF → 99 or clamp)
- **rtc_bcd_leap_year**: BCD date conversion near leap year boundary
- **rtc_read_fails_when_updating**: RTC update-in-progress flag handled

#### CheckedPtr
- **checked_ptr_out_of_bounds**: Read/write beyond CheckedPtr bounds triggers fault
- **checked_ptr_null_dereference**: Dereferencing null CheckedPtr is caught
- **checked_ptr_wrap_overflow**: Address + size wraps around (integer overflow)
- **checked_ptr_kernel_range_rejected**: Kernel-space addresses rejected in user mode checks

### Demo/Integration Tests

Tests that verify full-subsystem integration via observable output.

#### Mandelbrot Framebuffer Demo (CRC Hash Verification)
- Render the mandelbrot demo to the framebuffer
- Compute CRC32 of the framebuffer region after rendering
- Assert hash matches a known-good reference value
- Fail on: corrupted rendering pipeline, broken framebuffer driver, wrong VBE mode, memory corruption
- Workflow: run demo once, visually verify correctness, record CRC32 hash as test expectation
- Implementation: `hash::crc32_fb(start_addr, width * height * bpp/8)` computed in-kernel, printed via serial, asserted in test runner
- Detection: Any framebuffer regression changes the hash — caught automatically by `make test-qemu`

### Structual Test Issues (To Fix)
- **Duplicate registration functions**: test_scheduler.cpp, test_task.cpp, test_driver.cpp, test_vfs.cpp, test_syscall.cpp each have two `register_*_tests()` definitions (linker error)
- **Orphaned tests**: test_ipc.cpp (2), test_sync.cpp (3) define tests but never register them
- **Overlapping tests**: test_scheduler.cpp duplicates 3 tests from test_idle_task.cpp

### Next Phase: Phase 2 - Filesystems & Shell UX (0.2.10-0.2.11)

#### Files To Create (6 files, ~85 tests)
1. test_block_device.cpp (10 tests) - ATA PIO, block layer
2. test_fat32.cpp (30 tests) - FAT32 driver core
3. test_vfs_fat32.cpp (7 tests) - VFS integration
4. test_shell_ux.cpp (20 tests) - Shell builtins, pipes, status bar
5. test_syscall_dir.cpp (11 tests) - MKDIR, UNLINK, RMDIR syscalls
6. test_initrd_utils.cpp (7 tests) - User-space utilities

### Instructions for Phase 2
- Use mock block device interface (no real hardware)
- Test FAT32 edge cases: long names (rejected), case insensitivity, full disk, cluster chain corruption
- Shell tests: pipe chains, broken SIGPIPE, heredoc, job control, status bar metrics
- Directory syscalls: EEXIST, ENOTDIR, EACCES, ENOTEMPTY, EBUSY
- Initrd utilities: binary execution, argument parsing
