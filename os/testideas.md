# Test Ideas & Implementation Status

## Current Phase: Phase 1 - Microkernel Foundation (0.2.9) - COMPLETED

### Completed Test Files (Phase 1)
- test_task_lifecycle.cpp - 7 tests (task exit cleanup, blocked senders, page tables, reparenting, zombie, reap deferral, ELF init_task_common)
- test_idle_task.cpp - 8 tests (idle creation, ring3, priority, pause syscall, yield, hlt fallback, restart, singleton)
- test_vfsd.cpp - 12 tests (VFS daemon IPC server)
- test_iocd.cpp - 8 tests (I/O daemon driver server)
- test_capability.cpp - 21 tests (capability-based MMIO access, extended from 6)

Total Phase 1: 56 new tests added (was 116, now 172 kernel tests + framework = 205 total)

### Implementation Instructions for AI

CRITICAL: Never use the write tool. Use bash with cat << 'EOF' > filename instead.

Example:
cat > /path/to/file.cpp << 'EOF'
#include <test.hpp>
#include <logger.hpp>
// ... test code ...
EOF

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
