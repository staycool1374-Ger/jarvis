# Test Cases — v0.3.5 (Phase 4: Deterministic Memory & Resource Management)

All tests implemented, registered, and passing (39/39) on `main` at `d5f74263`.

### Static Memory Pools — test_static_pools.cpp (4+2 tests, PASS)
- `static_pools_mempool_reserve_success` — `MemPool::reserve(0, 1)` succeeds, pool count decrements
- `static_pools_mempool_reserve_exhaustion` — reserving `total+1` from pool 0 fails gracefully
- `static_pools_mempool_reserve_then_alloc` — reserved block is allocatable via `MemPool::alloc`
- `static_pools_mempool_reserve_all_then_alloc_fails` — reserving all blocks makes next `alloc` return nullptr
- `static_pools_pmm_disabled_after_init` — guarded by `CONFIG_STATIC_POOLS_ONLY`: `PMM::alloc_page()` returns 0 after `mark_init_done()`
- `static_pools_contiguous_disabled_after_init` — guarded by `CONFIG_STATIC_POOLS_ONLY`: `PMM::alloc_contiguous()` returns 0 after `mark_init_done()`

### Kernel Stack Usage Profiler — test_stack_profiler.cpp (6 tests, PASS)
- `stack_profiler_task_has_valid_stack` — created task has non-null `kernel_stack` and valid `kernel_stack_top`
- `stack_profiler_stack_usage_bounded` — created task's stack size equals `STACK_SIZE`
- `stack_profiler_context_rsp_in_range` — initial context RSP (`context.rsp`) falls within kernel stack bounds
- `stack_profiler_resets_on_cleanup` — after `cleanup()`, `kernel_stack` is null and `kernel_stack_top` is 0
- `stack_profiler_current_task_stack_valid` — current task's RSP is within its own stack and size matches `STACK_SIZE`
- `stack_profiler_user_task_stack_size` — created user task has `user_stack_size_ == 64_KiB` and non-null `user_stack_`

### Stack Allocation — test_stack_alloc.cpp (8 tests, PASS)
- `stack_alloc_default_size_correct` — kernel stack size equals `CONFIG_STACK_SIZE`
- `stack_alloc_task_has_stack_phys` — created task has non-zero `stack_phys_`
- `stack_alloc_user_task_has_guard_page` — user task has `user_stack_guard_page_` set
- `stack_alloc_stack_alignment` — kernel stack base is page-aligned
- `stack_alloc_multiple_tasks_distinct_stacks` — two created tasks have different kernel stacks
- `stack_alloc_overflow_hook_weak_symbol` — overflow hook weak symbol exists
- `stack_alloc_user_stack_phys_freed_on_cleanup` — `user_stack_` is zeroed after `cleanup()`
- `stack_alloc_user_stack_size_in_config` — user stack size matches `CONFIG_USER_STACK_SIZE`

### Page Tables — test_page_tables.cpp (7 tests, PASS)
- `page_tables_alloc_from_pool` — page-table pool alloc returns valid physical page
- `page_tables_pool_multiple_allocs` — pool handles sequential allocations
- `page_tables_pool_size_configured` — pool size matches `CONFIG_PAGE_TABLE_POOL_SIZE`
- `page_tables_kernel_task_no_page_table` — kernel task has `page_table_ == 0`
- `page_tables_user_task_page_table_set` — user task has non-zero `page_table_`
- `page_tables_free_pages_on_cleanup` — pages freed on task cleanup
- `page_tables_max_process_pages_config` — `CONFIG_MAX_PROCESS_PAGES` is non-zero

### Buffer Pool / IPC — test_buffer_pool_deterministic.cpp (6 tests, PASS)
- `buffer_pool_deterministic_preallocated_pool` — buffer pool init claims pre-allocated pages
- `buffer_pool_deterministic_no_dynamic_alloc` — no PMM alloc occurs after pool init
- `buffer_pool_deterministic_exhaustion_returns_zero` — exhausted pool returns nullptr
- `buffer_pool_deterministic_zero_copy_transfer` — buffer transfer via handle (no copy)
- `buffer_pool_deterministic_inline_payload` — inline payload in buffer descriptor
- `buffer_pool_deterministic_pool_config_consistent` — pool config matches `CONFIG_BUFFER_POOL_BLOCKS`

### Eliminate operator new/delete — test_no_op_new.cpp (6 tests, PASS)
- `no_op_new_placement_new_in_static` — placement new into static buffer works
- `no_op_new_mempool_alloc_free_cycle` — `MemPool::alloc`/`free` cycle succeeds
- `no_op_new_mempool_multiple_sizes` — alloc from multiple MemPool size classes
- `no_op_new_placement_new_array_static` — placement new array into static buffer
- `no_op_new_placement_new_nested_struct` — placement new with nested struct
- `no_op_new_mempool_reuse_after_free` — freed block can be re-allocated
