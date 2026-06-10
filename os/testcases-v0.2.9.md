# Test Cases — v0.2.9 (Phase 1: Microkernel Foundation)

## Branch: testbed only

## Remaining Coverage Gaps (Existing Subsystems)

### PMM (Physical Memory Manager)
- **pmm_double_free_rejected**: Freeing an already-freed page should be detected (or idempotent)
- **pmm_free_invalid_address**: Freeing a non-allocated/invalid page address
- **pmm_alloc_contiguous_zero**: `alloc_contiguous(0)` should fail gracefully
- **pmm_alloc_contiguous_exhaustion**: Allocate beyond available memory returns 0
- **pmm_alloc_user_page_oob**: User page allocation when user pool is exhausted
- **pmm_page_table_alloc_from_low_mem**: Verify page-table pages come from <128 MiB pool (partial coverage exists)

### VMM (Virtual Memory Manager)
- **vmm_unmap_already_unmapped**: `unmap_page` on an unmapped VA should be safe (idempotent)
- **vmm_map_already_mapped**: `map_page` on a VA that already has a physical page mapped (should fail or unmap first)
- **vmm_map_page_null_phys**: `map_page` with phys=0 should fail
- **vmm_clone_failure_rollback**: When `clone_kernel_pml4` runs out of memory, partial allocations are freed
- **vmm_free_user_pages_shared**: Freeing user pages on a shared (forked) PML4 does not free pages still in use by parent
- **vmm_huge_page_split_corner**: Split at PD boundary (address at 2 MiB-aligned edge)

### IPC / MessageQueue
- **ipc_send_data_size_exceeds_max**: `data_size > IPC_MAX_MSG_SIZE` rejected
- **ipc_send_data_size_zero**: Data_size=0 is valid (boundary)
- **ipc_queue_remove_from_mid**: Blocked sender removed from middle of list (not just head/tail)
- **ipc_multiple_blocked_senders_wake_one**: Multiple blocked senders, wake one at a time via recv
- **ipc_send_sync_timeout**: Synchronous send with timeout expires
- **ipc_priority_inversion**: Low-priority task holds resource, high-priority blocks — priority inheritance verified
- **ipc_send_self_max_message_size**: Verify 64-byte max payload round-trips correctly

### Scheduler
- **scheduler_same_priority_round_robin**: Tasks at same priority get equal timeslices
- **scheduler_priority_inheritance**: Priority ceiling/inheritance protocol on lock contention
- **scheduler_remove_nonexistent**: `remove_task` with invalid ID returns gracefully
- **scheduler_add_duplicate**: Adding same TCB twice does not corrupt task list
- **scheduler_tick_time_slice**: Verify timeslice decrement and preemption on tick

### ELF Loader
- **elf_load_multiple_segments**: Multiple PT_LOAD entries at different VAs loaded correctly
- **elf_load_bss_segment**: PT_LOAD with filesz=0, memsz>0 (BSS) zero-filled
- **elf_load_unaligned_vaddr**: Segment `vaddr` not page-aligned (should page-align)
- **elf_load_invalid_phdr_flags**: Exec+Writable segment rejected (W^X violation)
- **elf_load_phoff_out_of_bounds**: Program header offset past end of binary
- **elf_load_overlapping_segments**: PT_LOAD entries with overlapping VAs
- **elf_load_segment_too_large**: `memsz` > remaining address space

### Syscalls
- **syscall_open_null_path**: OPEN with null path -> EFAULT
- **syscall_read_invalid_fd**: READ with invalid fd -> EBADF
- **syscall_read_null_buffer**: READ with null buffer -> EFAULT
- **syscall_write_invalid_fd**: WRITE with invalid fd -> EBADF
- **syscall_write_null_buffer**: WRITE with null buffer -> EFAULT
- **syscall_kill_invalid_signal**: KILL with out-of-range signal number
- **syscall_kill_nonexistent_pid**: KILL to PID that does not exist
- **syscall_fork_shared_page_tables**: Fork verifies `page_table_shared_` flag
- **syscall_exec_invalid_elf**: EXEC with valid path but corrupted ELF
- **syscall_open_max_fds**: Exhausting FD table returns EMFILE
- **syscall_pipe_null_buffer**: PIPE with null pipefd pointer -> EFAULT
- **syscall_signal_sigkill_ignored**: SIGKILL handler cannot be set (should be rejected)
- **syscall_signal_sigstop_ignored**: SIGSTOP handler cannot be set

### Sync Primitives
- **mutex_trylock_already_locked**: `try_lock` on held mutex returns false
- **mutex_unlock_non_owner**: Unlock by non-owning task should fail
- **mutex_recursive_lock_block**: Same task locking mutex twice blocks (or deadlock detection)
- **semaphore_init_count_above_max**: Semaphore init with count > limit clamps or fails
- **semaphore_post_above_max**: Post beyond max count clamps
- **queue_receive_null_size**: `receive` with null size pointer
- **queue_send_null_data**: `send` with null data pointer
- **queue_receive_from_empty_nonblock**: `try_receive` on empty queue returns false

### Signals
- **signal_sigkill_not_catchable**: Setting handler for SIGKILL is rejected
- **signal_sigstop_not_catchable**: Setting handler for SIGSTOP is rejected
- **signal_nested_delivery**: Signal delivered while handler is already running
- **signal_mask_block**: Blocked signals are pending but not delivered
- **signal_during_syscall**: Signal arrives during blocking syscall (interruptible)
- **signal_default_stop**: SIGTSTP default action is STOP (verify)

### Capabilities (for when APIs exist — remove stub status)
- **cap_auto_revoke_on_exit**: Task capabilities revoked when task exits
- **cap_double_revoke**: Revoking already-revoked capability is idempotent

### Task Lifecycle
- **task_create_invalid_priority**: Priority below 0 or above max -> creation fails
- **task_create_zero_stack**: Zero stack size -> creation fails or uses minimum
- **task_fork_at_max_fds**: Fork when FD table is full -> child gets empty table or fails gracefully
- **task_exit_during_syscall**: Task exits while blocked on IPC send (cleanup)
- **task_reparent_to_nonexistent**: Reparent to PID that doesn't exist should fall back to init
- **fork_waitpid_child_exits_before_wait**: Fork, child calls `_exit(42)`, parent then calls `waitpid()` — verifies the child is found and its exit code returned. Regression for `reap_orphans()` prematurely reaping children of alive parents.

### RTC
- **rtc_bcd_invalid**: BCD values above 0x99 map correctly (e.g., 0xFF -> 99 or clamp)
- **rtc_bcd_leap_year**: BCD date conversion near leap year boundary
- **rtc_read_fails_when_updating**: RTC update-in-progress flag handled

### CheckedPtr
- **checked_ptr_out_of_bounds**: Read/write beyond CheckedPtr bounds triggers fault
- **checked_ptr_null_dereference**: Dereferencing null CheckedPtr is caught
- **checked_ptr_wrap_overflow**: Address + size wraps around (integer overflow)
- **checked_ptr_kernel_range_rejected**: Kernel-space addresses rejected in user mode checks

## New Test Files

### Timer/PIT (test_timer.cpp)
- **pit_init_sets_divisor**: After init, PIT counter divisor matches expected value
- **pit_ticks_monotonic**: Successive `ticks()` calls return non-decreasing values
- **pit_set_ticks_for_test**: `set_ticks_for_test()` overrides the tick counter
- **pit_irq0_handler_increments_ticks**: Each IRQ0 delivery increments the tick count
- **pit_set_frequency_accepts_range**: Frequency clamping at boundaries (min/max)

### Serial (test_serial.cpp)
- **serial_init_configures_baud**: COM1 baud rate set to 115200 after init
- **serial_putchar_output**: Character written to serial TX register (requires mock or loopback)
- **serial_puts_appends_crlf**: `puts()` appends `\r\n` to every string
- **serial_puts_empty_string**: `puts("")` outputs only CRLF

### Keyboard (test_keyboard.cpp)
- **keyboard_init_clears_buffer**: Ring buffer empty after keyboard init
- **keyboard_enqueue_dequeue_scancode**: Scancode enqueued then read back in order
- **keyboard_buffer_full_drops**: When 256-byte ring buffer full, new scancodes dropped (no overflow)
- **keyboard_modifier_tracking**: Shift/Alt/Ctrl state bits toggle correctly on make/break
- **keyboard_self_test_sequence**: PS/2 controller self-test executed and acknowledged

### GDT/TSS (test_gdt.cpp)
- **gdt_entries_valid_after_init**: All GDT entries have correct base=0, limit, access byte
- **gdt_tss_ist_valid**: TSS IST1-IST7 pointers point to valid kernel stack pages
- **gdt_tss_rsp0_set**: RSP0 points to kernel stack for ring 3 -> ring 0 transitions
- **gdt_code_data_segments_present**: Code (ring 0 + ring 3) and data segments are accessible
- **gdt_user_segments_have_ring3_dpl**: User code/data segment descriptors have DPL=3

### IDT (test_idt.cpp)
- **idt_entries_initialized**: All 256 IDT entries have non-null handler addresses after init
- **idt_exception_handlers_mapped**: CPU exceptions 0-31 have handler entries (no gaps)
- **idt_irq_remapped**: PIC IRQ0-IRQ15 mapped to interrupt vectors 0x20-0x2F
- **idt_syscall_handler_installed**: Interrupt 0x80 handler points to syscall dispatch
- **idt_double_fault_uses_ist**: Double fault handler uses TSS IST stack (not kernel stack)
- **idt_reserved_vectors_null**: Vectors 0x30-0x7F are not set (or point to spurious handler)

### Boot Parameters (test_bootparams.cpp)
- **bootparams_parse_debug_flags**: `debug_ipc=1` in cmdline activates the IPC debug flag
- **bootparams_parse_multiple_flags**: Multiple `debug_*` flags parsed from single cmdline
- **bootparams_empty_cmdline**: No command line — all debug flags stay false
- **bootparams_malformed_cmdline**: Garbage string parsed without crash (ignores unknown flags)

### Multiboot (test_multiboot.cpp)
- **mb2_find_tag_by_type**: Each known tag type (framebuffer, memory map, module, cmdline) found
- **mb2_find_tag_nonexistent**: Unknown tag type returns nullptr
- **mb2_framebuffer_tag_fields**: Framebuffer tag struct width/height/bpp correctly populated
- **mb2_memory_map_tag_entries**: Memory map tag parses entry count and entry size correctly
- **mb2_module_tag_start_end**: Module tag returns valid start/end addresses

### Address Wrappers (test_address.cpp)
- **address_phys_to_virt_identity**: `phys_to_virt(HHDM_BASE)` returns `HHDM_BASE`
- **address_virt_to_phys_identity**: `virt_to_phys(HHDM_BASE)` returns `HHDM_BASE`
- **address_page_align_down**: PageAddress aligns a non-aligned address down to 4 KiB boundary
- **address_page_offset**: VirtualAddress offset within page extracted correctly
- **address_null_phys**: PhysicalAddress(0) == null PhysicalAddress
- **address_comparison_operators**: `<`, `>`, `==`, `!=` work correctly across address objects

### Pipe (test_pipe.cpp)
- **pipe_create_returns_two_fds**: `create_pipe()` returns two valid file descriptors
- **pipe_read_from_empty_nonblock**: Non-blocking read from empty pipe returns -1 with errno EAGAIN
- **pipe_write_to_full_blocks**: Writing beyond pipe buffer capacity blocks the writer
- **pipe_read_end_closed_returns_epipe**: Writing to pipe with closed read end returns EPIPE
- **pipe_write_then_read_roundtrip**: Written data read back identically in FIFO order
- **pipe_multiple_writers_no_interleaving**: Concurrent writes are not interleaved (PIPE_BUF atomicity)

### VFS Internal Filesystems (test_vfs_internal.cpp)
- **devfs_read_null**: Reading `/dev/null` returns 0 bytes
- **devfs_read_zero**: Reading `/dev/zero` returns NUL-filled buffer of requested size
- **devfs_tty_resolves**: `/dev/tty` resolves to a valid inode (actual read requires keyboard)
- **procfs_self_resolves**: `/proc/self` resolves to the current process PID
- **procfs_meminfo_valid**: Reading `/proc/meminfo` returns well-formatted ASCII data
- **initrdfs_list_root**: Directory listing `/` returns initrd contents
- **initrdfs_read_known_file**: Reading a known initrd file returns expected byte content
- **initrdfs_open_nonexistent**: Opening a non-existent initrd path returns ENOENT

### GCOV / Profiling (test_gcov.cpp)
- **gcov_handler_initialized**: After kernel boot, gcov handler struct is in valid state
- **gcov_instrument_updates_bitmask**: Calling instrument function toggles the tracking bitmask
- **gcov_flush_outputs_data**: `gcov_flush_to_serial()` emits non-empty trace data
- **rdtsc_monotonic**: Consecutive `rdtsc()` calls return strictly increasing values

### Debug Facilities (test_debug.cpp)
- **qemu_debug_exit_success**: Exit code 0 shuts down QEMU cleanly via port 0x501
- **qemu_debug_exit_failure**: Non-zero exit code correctly reported by QEMU test harness
- **debug_write_formats_correctly**: `debug_write()` outputs hex/dec numbers with correct formatting
- **debug_task_switch_logs**: Task switch generates a log message with source -> dest PID

### Framebuffer (test_framebuffer.cpp)
- **fb_init_from_multiboot**: Framebuffer initializes from multiboot framebuffer tag
- **fb_putpixel_in_bounds**: Writing at valid (x, y) sets the correct pixel color
- **fb_putpixel_out_of_bounds**: Writing outside framebuffer bounds is safe (no-op or clamped)
- **fb_clear_screen**: Clearing sets all pixels to the background color
- **fb_scroll_up**: Scrolling shifts framebuffer content up by one line correctly

### Integration / Stress (test_stress.cpp)
- **stress_1000_tasks_create_exit**: Create 1000 tasks, each exits immediately — scheduler remains consistent
- **stress_ipc_blast**: 1000 send/receive round-trips between two kernel tasks — no lost messages
- **stress_memory_alloc_free_loop**: Allocate and free all available PMM pages repeatedly — no leaks
- **stress_recursive_fork_depth_10**: Fork chain 10 deep, all children exit — no orphan leaks
- **stress_all_syscalls_sequential**: Every syscall invoked at least once in sequence — no crashes
- **stress_syscall_fuzz_random**: Random syscall numbers (0-255) with random args — no kernel panics

### PIC (8259A) (test_pic.cpp)
- **pic_remap_vectors**: IRQ0-IRQ7 remapped from 0x08-0x0F to 0x20-0x27, IRQ8-IRQ15 to 0x28-0x2F
- **pic_mask_all**: All IRQ lines masked after init (except cascade for slave)
- **pic_ocw2_end_of_interrupt**: Sending EOI to PIC clears the in-service register

### Demo / Integration
- **mandelbrot_crc_hash**: Render Mandelbrot to framebuffer, compute CRC32, assert known-good hash. Detects corrupted rendering pipeline, broken framebuffer driver, wrong VBE mode, memory corruption.

## Roadmap-Specific Test Ideas (Unchecked 0.2.9 Items)

These test ideas correspond to roadmap items in 0.2.9 that remain unchecked.

### Task Lifecycle Audit
- **lifecycle_zombie_no_waker**: Terminated task with no parent and no waker is reaped
- **lifecycle_ipc_objects_null_after_exit**: All IPC objects (msg_queue, notify, event_group) are null after task exit
- **lifecycle_blocked_senders_woken**: All blocked senders are woken when receiver exits (regression)
- **lifecycle_fork_page_table_shared_flag**: Forked child has `page_table_shared_=true`, child cleanup skips free_user_pages

### Userspace Idle Task
- **idle_userspace_created**: Userspace idle task exists in scheduler after boot
- **idle_userspace_lowest_priority**: Userspace idle has lowest priority of all user tasks
- **idle_userspace_pause_loop**: Userspace idle runs `pause` instruction (not `hlt`)
- **idle_kernel_fallback_exists**: Kernel `hlt` idle still exists at `tasks_[0]` as fallback

### User-space VFS Server
- **vfsd_boots_and_registers**: VFS daemon boots and registers with kernel (replace stub)
- **vfsd_handle_open**: VFS daemon handles open message (replace stub)
- **vfsd_handle_read_write**: VFS daemon handles read/write messages (replace stub)
- **vfsd_handle_resolve**: VFS daemon handles path resolve message (replace stub)
- **vfsd_handle_mount_unmount**: VFS daemon handles mount/unmount messages (replace stub)
- **vfsd_handle_stat_fstat**: VFS daemon handles stat/fstat messages (replace stub)
- **vfsd_invalid_message_type_rejected**: Invalid message type rejected (replace stub)

### User-space Driver Server
- **iocd_boots_and_registers**: IOCD boots and registers with kernel (replace stub)
- **iocd_keyboard_irq_to_event**: Keyboard IRQ converted to IPC event (replace stub)
- **iocd_serial_irq_to_event**: Serial IRQ converted to IPC event (replace stub)
- **iocd_mmio_map_via_capability**: MMIO region mapped via capability (replace stub)

### Capability-based Hardware Access
- **cap_create_for_mmio**: Capability created for MMIO region with valid phys + size
- **cap_grant_to_task**: Capability granted to target task
- **cap_map_mmio_via_cap**: MMIO mapped via capability (replace stub)
- **cap_revoke_removes_access**: Revoked capability cannot be used (replace stub)
- **cap_forgery_rejected**: Random/incremented cap values rejected (replace stub)
- **cap_bounds_validated**: Zero size, invalid phys addr rejected (replace stub)
- **cap_nonexistent_task_grant**: Granting to nonexistent task fails (replace stub)
- **cap_duplicate_grant_rejected**: Duplicate grant to same task rejected (replace stub)
