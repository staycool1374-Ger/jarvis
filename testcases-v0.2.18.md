# Test Cases — v0.2.18 (Observability & Portability)

### Kernel Log Ring Buffer — test_klog.cpp (5 tests)
- `klog_read_entries` — generate log entries, read via RingBuffer::read(), verify content
- `klog_ringbuffer_wrap` — fill buffer 2× capacity, verify read returns ≤ BUFFER_SIZE bytes
- `klog_concurrent_readers` — two independent readers on same ring buffer both succeed
- `klog_invalid_buffer_eFault` — CheckedPtr rejects NULL, kernel-space address, stack buffer
- `dmesg_prints_kernel_log` — write test marker, read back, verify contains marker string

### HAL Abstraction — test_hal.cpp (12 tests)
- `hal_page_table_map_unmap` — map a virtual page, verify access, unmap, verify fault
- `hal_page_table_clone` — clone page table, modify clone, verify original unchanged
- `hal_page_table_switch` — two page tables with different mappings, switch between them
- `hal_context_save_restore` — fill registers with known pattern, save, modify, restore, verify
- `hal_context_switch_to` — switch_to between two kernel tasks, verify control transfers
- `hal_interrupt_mask_unmask` — mask IRQ 1, verify not delivered, unmask, verify delivery
- `hal_interrupt_eoi` — trigger interrupt, no EOI blocks same-priority, EOI unblocks
- `hal_timer_oneshot` — set one-shot timer for N ticks, verify fires exactly once
- `hal_timer_periodic` — set periodic timer, verify fires at t+P, t+2P, t+3P
- `hal_timer_remaining` — query remaining time at various points, verify decreasing
- `hal_io_byte_word_long` — outb/inb, outw/inw, outl/inl via diagnostic port 0x80
- `hal_delegates_to_x86_64` — verify HAL methods resolve to arch/x86_64/ implementations

### Arch Structure / Multi-Arch Build — test_arch_structure.cpp (4 tests)
- `arch_x86_64_isolation` — verify x86_64-specific code lives only in arch/x86_64/
- `arch_generic_interfaces_only` — arch/ contains only .hpp interface headers, no .cpp
- `buildsystem_arch_selection` — make ARCH=x86_64 succeeds, make ARCH=invalid fails
- `hal_interfaces_exist` — verify all 5 HAL interface headers exist (page_table, context, interrupt_controller, timer, io)

### Build System — test_buildsystem.cpp (4 tests)
- `buildsystem_arch_x86_64_toolchain` — ARCH=x86_64 selects x86_64-elf- toolchain
- `buildsystem_invalid_arch_error` — invalid ARCH value produces clear error message
- `buildsystem_default_arch_x86_64` — default build uses x86_64 without ARCH variable
- `buildsystem_debug_flags` — debug build includes -DCONFIG_DEBUG, -g, -Og flags

### Secure Exec — test_secure_exec.cpp (5 tests)
- `secure_exec_valid_argv_envp` — user-space addresses pass is_user_range/CheckedPtr validation
- `secure_exec_null_argv_eFault` — NULL addresses rejected by is_user_range/CheckedPtr
- `secure_exec_kernel_space_argv_eFault` — kernel-space addresses (>= HHDM) rejected
- `secure_exec_unmapped_crossing_eFault` — range crossing user/kernel boundary rejected
- `secure_exec_regression_audit` — systematic audit of all secure-exec paths: argv, envp, string arrays, partial pages, edge-of-limit addresses

### PCI Bus Enumeration — test_pci.cpp (1 new test)
- `pci_print_tree_output` — call pci_print_tree() and verify it produces valid device lines with vendor:device IDs, class/subclass, and BAR info

### Sporadic Server — test_sporadic_server.cpp (14 tests)
- `sporadic_server_init` — init with C=2, T=10, verify budget=T, bg_prio=0, state=IDLE
- `sporadic_server_activation` — on_activation, verify state=ACTIVE, current_priority=base
- `sporadic_server_double_activation` — two activations while ACTIVE, verify no double-accounting
- `sporadic_server_exhaustion` — consume budget to 0, verify state=EXHAUSTED, current_priority=bg
- `sporadic_server_completion` — on_completion in ACTIVE state, verify replenishment scheduled
- `sporadic_server_replenishment_restores_budget` — exhaust, complete, process replenishments, verify budget restored
- `sporadic_server_budget_capped` — schedule 2× replenishments, process both, verify budget ≤ C
- `sporadic_server_queue_overflow` — schedule 16 replenishments on 8-slot queue, verify drops handled
- `sporadic_server_priority_transition` — ACTIVE→EXHAUSTED→bg→replenished→ACTIVE, verify priority at each step
- `sporadic_server_consume_returns_false` — consume past 0, verify returns false
- `sporadic_server_replenish_due_only` — schedule repl at t+10, process at t+5, verify no early restore
- `sporadic_server_multi_replenish_same_tick` — two completions, two replenishments fire at same tick, verify budget = C
- `sporadic_server_exhaust_then_activation` — exhaust, on_activation while EXHAUSTED, verify no state change
- `sporadic_server_idle_to_exhaust` — activate, consume all, on_completion, verify no crash on idle