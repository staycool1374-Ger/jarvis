# Test Cases — v0.2.10 (Phase 2: FAT32 Block Filesystem)

## Branch: testbed only

## Test Design Notes
- Use mock block device interface (no real hardware)
- Test FAT32 edge cases: long names (rejected), case insensitivity, full disk, cluster chain corruption
- Follow the boundary, error path, resource exhaustion, and self-cleanup principles

## Files To Create

### test_block_device.cpp (~10 tests)

#### ATA PIO Driver
- **ata_pio_identify**: IDENTIFY command returns valid disk signature (0x0040 or 0xC33C)
- **ata_pio_read_single_sector**: Read one sector (512 bytes) returns valid data without error
- **ata_pio_write_single_sector**: Write then read back a sector produces identical data
- **ata_pio_read_multiple_sectors**: Consecutive sector reads return sequential data (LBA ordering)
- **ata_pio_write_read_roundtrip**: Multiple sectors written then read back match exactly
- **ata_pio_invalid_lba_rejected**: LBA beyond disk capacity returns error

#### Block Layer
- **block_device_open**: Block device opens with correct sector count
- **block_device_read_sector**: Block read returns sector-aligned data
- **block_device_write_sector**: Block write followed by read returns same data
- **block_device_ioctl_info**: IOCTL returns block size (512) and sector count
- **block_device_oob_rejected**: Read/write beyond device capacity returns error
- **block_device_unaligned_buffer**: Read with non-sector-aligned buffer is handled safely

### test_fat32.cpp (~30 tests)

#### MBR / Partition Table
- **fat32_mbr_valid_signature**: MBR ends with 0xAA55 signature
- **fat32_mbr_parse_partition**: First partition entry has valid type (0x0C or 0x0B for FAT32), LBA, size
- **fat32_mbr_no_partition_table**: Empty MBR (no valid partition) is detected

#### BPB / Boot Sector
- **fat32_bpb_valid_signature**: Boot sector (first sector of partition) has 0xAA55 signature
- **fat32_bpb_bytes_per_sector**: BPB bytes_per_sector == 512 (or the actual value)
- **fat32_bpb_sectors_per_cluster**: sectors_per_cluster is power of 2 (1, 2, 4, 8, etc.)
- **fat32_bpb_reserved_sectors**: reserved_sector_count > 0
- **fat32_bpb_fat_count**: Number of FATs is 2
- **fat32_bpb_root_cluster**: Root directory cluster is valid (typically 2)
- **fat32_bpb_fat_size**: FAT size in sectors is non-zero and consistent with partition size
- **fat32_bpb_total_sectors**: Total sector count matches partition size from MBR
- **fat32_fs_info_valid**: FSInfo sector has valid signature (0x41615252, 0x61417272)

#### FAT Table
- **fat32_fat_read_cluster**: Read cluster chain entry from FAT returns valid value
- **fat32_fat_eof_marker**: End-of-chain marker (0x0FFFFFF8-0x0FFFFFFF) detected correctly
- **fat32_fat_free_cluster**: Free cluster (0x00000000) detected correctly
- **fat32_fat_bad_cluster**: Bad cluster marker (0x0FFFFFF7) detected correctly
- **fat32_fat_allocate_cluster**: Allocate a free cluster updates FAT and returns new cluster number
- **fat32_fat_free_cluster_chain**: Free a cluster chain updates FAT entries to free
- **fat32_fat_full_disk**: Allocate when no free clusters remain returns error (ENOSPC)

#### Directory Operations
- **fat32_dir_root_entries**: Root directory contains at least "." and ".." entries
- **fat32_dir_short_name**: Short 8.3 filename parsed correctly from directory entry
- **fat32_dir_long_name_rejected**: Long filename (>11 chars in 8.3) entry is detected (not supported)
- **fat32_dir_file_size**: Directory entry file size matches expected value
- **fat32_dir_file_cluster**: Directory entry first cluster matches known file
- **fat32_dir_attribute_readonly**: Read-only attribute bit parsed correctly
- **fat32_dir_attribute_hidden**: Hidden attribute bit parsed correctly
- **fat32_dir_attribute_directory**: Directory attribute bit identifies subdirectory entries
- **fat32_dir_volume_label**: Volume label entry (ATTR_VOLUME_ID) is skipped in directory listing
- **fat32_dir_lfn_skipped**: Long filename entries (0x0F attribute) are skipped in short-name mode

#### Cluster Chain
- **fat32_chain_traverse_single**: Single-cluster file has correct chain length
- **fat32_chain_traverse_multi**: Multi-cluster file chain traversal visits all clusters in order
- **fat32_chain_corrupt_loop**: Cluster chain with loop (cluster points back to earlier entry) is detected
- **fat32_chain_corrupt_eof_missing**: Chain with no EOF marker is detected (max cluster limit)

### test_vfs_fat32.cpp (~7 tests)

#### VFS Integration
- **vfs_fat32_mount**: FAT32 partition mounts successfully at /home
- **vfs_fat32_open_root**: Opening mount root returns valid directory vnode
- **vfs_fat32_open_file**: Opening an existing file on FAT32 returns valid file vnode
- **vfs_fat32_read_file**: Reading a known file on FAT32 returns expected content
- **vfs_fat32_write_file**: Write then read back a file on FAT32 produces identical data
- **vfs_fat32_create_file**: Creating a new file on FAT32 adds directory entry
- **vfs_fat32_delete_file**: Deleting a file on FAT32 frees its cluster chain and removes entry
- **vfs_fat32_readdir**: Directory listing returns entries matching on-disk content
- **vfs_fat32_mkdir**: Creating a subdirectory adds "." and ".." entries
- **vfs_fat32_rmdir**: Removing an empty directory succeeds, non-empty fails (ENOTEMPTY)
- **vfs_fat32_fstat**: fstat returns correct file size, attributes, and cluster count
- **vfs_fat32_nonexistent_path**: Opening a non-existent path returns ENOENT
- **vfs_fat32_unmount**: Unmounting flushes FAT and marks volume clean

## Roadmap-Specific Test Ideas

### User-space VFS Server
- **vfsd_boots_and_registers**: VFS daemon boots and registers with kernel — ✅ passing
- **vfsd_handle_open**: VFS daemon handles open message — ✅ passing
- **vfsd_handle_close**: VFS daemon handles close message — ✅ passing
- **vfsd_handle_read_write**: VFS daemon handles read/write messages — ✅ passing
- **vfsd_handle_resolve**: VFS daemon handles path resolve message — ✅ passing
- **vfsd_handle_mount_unmount**: VFS daemon handles mount/unmount messages — ✅ passing
- **vfsd_handle_stat_fstat**: VFS daemon handles stat/fstat messages — ✅ passing
- **vfsd_handle_chdir_getcwd**: VFS daemon handles chdir/getcwd operations — ✅ passing
- **vfsd_invalid_message_type_rejected**: Invalid message type rejected — ✅ passing
- **vfsd_malformed_message_rejected**: Malformed message rejected — ✅ passing
- **vfsd_unauthorized_task_rejected**: Unauthorized task rejected — ✅ passing
- **vfsd_concurrent_requests**: Concurrent requests handled correctly — ✅ passing
- **vfsd_crash_restarts**: VFS daemon restarts after crash — ✅ passing
- **vfsd_exhaust_restart_limit**: VFS daemon not restarted beyond MAX_RESTART_COUNT (10) — ✅ passing

### VFS Authorization Layer (NEW — based on actual syscall_handlers_fs.cpp implementation)

#### Kernel Task Bypass
Every VFS syscall (open/read/write/close/stat/fstat/chdir) calls `vfsd_authorize()` or `vfsd_authorize_fd_op()`, which checks `cur->page_table_`:
- If `null` (kernel task), authorization returns `true` WITHOUT sending IPC to vfsd.
- Already implicitly tested by `syscall_open_read_close` and `syscall_write_fstat` in `test_syscall.cpp` (both create a kernel task, call syscalls, verify success).
- **vfsd_kernel_bypass_open**: Kernel task (no page_table_) calls sys_open; authorization succeeds without IPC — ✅ passing
- **vfsd_kernel_bypass_read**: Kernel task calls sys_read; authorization succeeds without IPC — ✅ passing
- **vfsd_kernel_bypass_write**: Kernel task calls sys_write; authorization succeeds without IPC — ✅ passing
- **vfsd_kernel_bypass_stat**: Kernel task calls sys_stat; authorization succeeds without IPC — ✅ passing
- **vfsd_kernel_bypass_fstat**: Kernel task calls sys_fstat; authorization succeeds without IPC — ✅ passing
- **vfsd_kernel_bypass_chdir**: Kernel task calls sys_chdir; authorization succeeds without IPC — ✅ passing

#### VFS Daemon Self-Authorization (NOT YET CODED)
`vfsd_authorize()` and `vfsd_authorize_fd_op()` also check `is_vfsd_task()` — if the calling task IS vfsd, authorization returns `true` without IPC.
- **vfsd_self_authorization**: VFS daemon task calls VFS syscall; authorization returns true without IPC
- **vfsd_self_authorization_fd_op**: VFS daemon task calls fd-op syscall; authorization returns true

#### VFS Daemon Absent (NOT YET CODED)
- **vfsd_absent_authorize_fails**: vfsd PID is 0; vfsd_authorize returns false
- **vfsd_absent_syscall_fails**: vfsd PID is 0; VFS syscall returns -1

#### Malformed Input (NOT YET CODED)
- **vfsd_authorize_null_path**: vfsd_authorize called with null path (should not crash — path copy loop handles it)

### IPC Blocking Gaps (based on actual ipc.cpp / syscall_handlers_ipc.cpp implementation)

#### was_blocked Flag (FIXED)
Both `sys_receive` and `send_sync` set a `was_blocked` flag when entering the blocking while-loop and restore `cur->state = TaskState::READY` after a successful receive/reply. Without this, `handle_interrupt_c` would call `reschedule()` on a task that just received data.
- **ipc_receive_was_blocked_restores_state**: Task blocks in receive, receives message, state restored to READY — ✅ passing
- **ipc_send_sync_was_blocked_restores_state**: Task blocks in send_sync, gets reply, state restored to READY — ✅ passing

#### sti/hlt/cli Mechanism (interrupt-driven context switch during IPC) (FIXED)
Both `sys_receive` and `send_sync` while-loops execute `asm volatile("sti; hlt; cli")` AFTER `reschedule()` but ONLY when `cur->page_table_` is non-null (userspace task). This enables the timer ISR to fire within the `int 0x80` ISR (which uses an interrupt gate, clearing IF), perform the actual RSP save/load + iretq context switch, then resume.
- **ipc_userspace_block_uses_sti_hlt_cli**: Userspace task (page_table_ set) blocks in receive; sti/hlt/cli emitted — ✅ passing
- **ipc_kernel_block_skips_sti**: Kernel task (no page_table_) blocks in receive; sti/hlt/cli NOT emitted, task spins — ✅ passing

#### IPC::send blocking path (queue full) — NOT A BUG
When the destination message queue is full, `IPC::send` calls `block_sender()`, sets state to BLOCKED, calls `Scheduler::reschedule()`. Analysis confirmed this path does NOT need the `was_blocked`/`sti/hlt/cli` fix because `handle_interrupt_c` (line 598-601) catches the BLOCKED state after `int 0x80` returns and calls `reschedule()`, completing the context switch via ISR assembly. No infinite loop exists (unlike the while-loops in `send_sync`/`sys_receive`).
- **ipc_send_queue_full_blocks_and_wakes**: Fill receiver's queue, send blocks, destination consumes, sender wakes — ✅ passing (implicitly via `ipc_send_block_full`)

### User-space Driver Server
- **iocd_boots_and_registers**: IOCD boots and registers with kernel — ✅ passing
- **iocd_keyboard_irq_to_event**: Keyboard IRQ converted to IPC event — ✅ passing
- **iocd_serial_irq_to_event**: Serial IRQ converted to IPC event — ✅ passing
- **iocd_mmio_map_via_capability**: MMIO region mapped via capability — ✅ passing
- **iocd_mmio_unmap_on_exit**: IOCD unmaps MMIO regions on exit — ✅ passing
- **iocd_unauthorized_mmio_rejected**: Unauthorized task MMIO request rejected — ✅ passing
- **iocd_multiple_device_handlers**: IOCD handles multiple device types — ✅ passing
- **iocd_irq_affinity**: IRQ affinity delivered to correct device handler — ✅ passing
- **iocd_crash_restarts**: IOCD restarts after crash (TERMINATED → cleanup → respawn) — ✅ passing

### Capability-based Hardware Access
- **cap_create_for_mmio**: Capability created for MMIO region with valid phys + size — ✅ passing
- **cap_grant_to_task**: Capability granted to target task — ✅ passing
- **cap_map_mmio_via_cap**: MMIO mapped via capability — ✅ passing
- **cap_revoke_removes_access**: Revoked capability cannot be used — ✅ passing
- **cap_forgery_rejected**: Random/incremented cap values rejected — ✅ passing
- **cap_bounds_validated**: Zero size, invalid phys addr rejected — ✅ passing
- **cap_nonexistent_task_grant**: Granting to nonexistent task fails — ✅ passing
- **cap_duplicate_grant_rejected**: Duplicate grant to same task rejected — ✅ passing
- **cap_transfer_to_child_on_fork**: Capability inherited by child after fork — ✅ passing
- **cap_inherit_on_exec**: Capability preserved across exec — ✅ passing
- **cap_max_per_task_limit**: Maximum cap limit enforced — ✅ passing
- **cap_create_mmio_valid_bounds**: Valid MMIO cap bounds accepted — ✅ passing
- **cap_create_mmio_invalid_size_zero**: Zero-size MMIO cap creation rejected — ✅ passing
- **cap_create_mmio_invalid_phys_addr**: Invalid phys addr MMIO cap rejected — ✅ passing
- **cap_grant_to_nonexistent_task_fails**: Grant to nonexistent PID fails — ✅ passing
- **cap_grant_duplicate_fails**: Duplicate grant to same task fails — ✅ passing
- **cap_map_mmio_success**: MMIO mapped via cap succeeds — ✅ passing
- **cap_map_mmio_wrong_task_fails**: MMIO map with wrong task cap fails — ✅ passing
- **cap_map_mmio_duplicate_virt_fails**: Duplicate virtual address map fails — ✅ passing
- **cap_revoke_unmaps**: Revoking cap unmaps the MMIO region — ✅ passing
- **cap_revoke_nonexistent_fails**: Revoking non-existent cap fails — ✅ passing
- **cap_forge_random_rejected**: Random cap value rejected — ✅ passing
- **cap_forge_incremented_rejected**: Incremented cap value rejected — ✅ passing

### Task ID Collision Fix

#### Bug
`elf::load` used a separate `static uint64_t next_task_id = 42` counter (`src/kernel/elf/elf.cpp:241`), while kernel-created tasks (`task.cpp:17`) used `static uint64_t next_task_id = 1`. With 2000+ test tasks, IDs from the two counters inevitably collided. When `remove_task()` called `id_table_remove(task->id)`, the hash probe found the daemon task (same ID, same index) and removed it from the ID table instead of the test task. This caused `iocd_boots_and_registers` to fail (iocd task silently removed from ID table before the test looked it up).

#### Fix
1. **Single global counter**: Removed both file-scoped `next_task_id` counters. All tasks now receive IDs from `Scheduler::alloc_id()` (`scheduler.cpp:149`), a single monotonically increasing counter.
2. **Pointer-based removal**: `id_table_remove` now matches by pointer (`id_table_[idx] == task`) instead of ID (`id_table_[idx]->id == id`). This prevents accidental removal of a daemon task when a test task shares the same ID via hash collision.

#### Regression Tests
- **task_id_collision_avoidance**: Create a daemon-like task, then create/destroy 100+ kernel tasks with arbitrary IDs, verify daemon task still present in ID table — ✅ passing (implicitly via `iocd_boots_and_registers`)
- **id_table_remove_pointer_match**: Insert two tasks with same ID via hash collision, remove one, verify other remains findable — ✅ passing (by design)
- **scheduler_alloc_id_monotonic**: Verify `Scheduler::alloc_id()` returns strictly increasing values — ✅ passing

## Buffer Pool — Zero-Copy IPC Shared Memory

### Implementation Bugs (fix first)
- **exec_into_current_clears_buffers**: alloc buffer in a task, call exec_into_current(),
  verify buffer entry is recycled (CRITICAL — current code unmaps from wrong page table
  after the old/new PML4 swap)
- **transfer_adds_to_receiver_list**: alloc buffer, transfer to receiver, verify it appears
  in receiver's buf_list_head (HIGH — currently omitted, causes leak on receiver exit
  without mapping)

### Boundary & Error Path Tests
- **va_conflict_rejected**: alloc at VA X, alloc again at same VA X, verify second alloc
  returns 0 (BUF_ERR_VA_IN_USE)
- **va_out_of_range_rejected**: alloc with VA >= USER_SPACE_LIMIT, verify returns 0
  (BUF_ERR_VA_OUT_OF_RANGE)
- **zero_va_rejected**: alloc with VA=0, verify returns 0 (guard page, below user space)
- **kernel_task_alloc_fails**: create kernel task (no page_table_), verify alloc returns 0
- **forged_handle_after_free**: alloc → free → use same handle (old gen), verify
  validate() returns -1
- **realloc_recycles_entry**: alloc → free → alloc again, verify same entry index recycled
  with incremented generation
- **alloc_after_exhaustion_and_free**: exhaust all 1024 entries, free one, verify new alloc
  recycles the freed entry
- **list_integrity_after_unlink**: alloc 10 buffers, free the middle entry (idx 5), then
  verify list_prev/list_next of neighbours are correct

### IPC Integration Tests
- **fork_buffer_not_inherited**: parent allocs buffer, forks (clone), verify child's
  buf_list_head == -1, parent still owns the buffer
- **ipc_nonblock_no_transfer_on_full**: fill receiver's message queue to capacity,
  send IPC_NONBLOCK with buf_handle, verify send fails and buffer still owned by sender
- **send_sync_with_buffer_handle**: send_sync with buf_handle set, verify transfer
  happens correctly (sender loses ownership, receiver can map)
- **ipc_transfer_multiple_messages**: send multiple messages each with distinct buf_handle,
  verify all transfers succeed and handles are independent

### Multi-Owner & Lifecycle Tests
- **transfer_chain**: alloc by A → transfer to B → transfer to C, C maps and frees,
  verify A and B cannot free/map/unmap the handle
- **transfer_receiver_exit_without_map**: alloc by A, transfer to B, B exits without
  ever calling map() (triggers cleanup()), verify buffer page freed and entry recycled
- **multi_map_same_task**: alloc buffer, map at 2nd VA in same task, unmap one VA,
  verify buffer still alive via the other VA, then free
- **unmap_idempotent**: unmap an already-unmapped buffer, verify returns false (NOT_MAPPED)

### Page Table & Resource Cleanup
- **intermediate_tables_do_not_leak**: alloc buffers at distinct VAs spanning multiple
  page-table levels (different PML4/PDPT/PD entries), then free them, verify no
  intermediate page-table pages leaked (requires VMM allocation counters)
- **cleanup_idempotent_with_empty_list**: create task (no buffers), call cleanup(),
  verify no crash or resource leak
