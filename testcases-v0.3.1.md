# Test Cases — v0.3.1 (O(1) Deterministic Scheduler)

## HAL Bits — test_hal_bits.cpp (14 tests)
- `find_highest_bit(0)` returns 0
- `find_highest_bit(1ULL)` returns 0 (LSB)
- `find_highest_bit(1ULL << 63)` returns 63 (MSB)
- `find_highest_bit(0xFF00ULL)` returns 15 (middle)
- `find_highest_bit(0xFFFFFFFFFFFFFFFFULL)` returns 63 (all bits)
- `find_highest_bit(0x8000000000000001ULL)` returns 63 (multi-bit, highest)
- `find_lowest_bit(0)` returns 0
- `find_lowest_bit(1ULL)` returns 0
- `find_lowest_bit(1ULL << 63)` returns 63
- `find_lowest_bit(0x8000000000000001ULL)` returns 0 (multi-bit, lowest)
- `find_lowest_bit(0xF0ULL)` returns 4
- `find_highest_bit(0x0F00ULL)` returns 11
- `find_highest_bit(0x8000000000000000ULL)` returns 63
- `find_lowest_bit(0x8000000000000000ULL)` returns 63

## PriorityMap — (4 tests)
- `o1_priority_map_set_get`: set/get/clear single priority 5
- `o1_priority_map_multiple`: set priorities 0,5,10,127 — verify highest in descending order
- `o1_priority_map_boundary`: edge cases at prio 0, 63, 64, 127
- `o1_priority_map_clear_nonexistent`: clearing unset priority leaves map empty

## TaskQueue — (3 tests)
- `o1_task_queue_single`: push/pop single TCB verifies empty/count
- `o1_task_queue_multiple`: push 3 TCBs, pop verifies FIFO order
- `o1_task_queue_remove_middle`: push 3 TCBs, remove middle, verify remaining order

## ReadyQueueManager — (4 tests)
- `o1_ready_queue_single`: enqueue/dequeue highest at single priority
- `o1_ready_queue_highest_priority`: enqueue at prios 5,10,15 — dequeue returns 15,10,5
- `o1_ready_queue_remove`: enqueue 2 tasks, remove higher, dequeue returns lower
- `o1_ready_queue_128_levels`: enqueue 128 tasks at all priorities, dequeue all from 127 down to 0

## Scheduler Integration — (2 tests)
- `o1_scheduler_dequeues_highest`: add_task creates 2 tasks (prio 5, 15), next_task returns prio 15
- `o1_scheduler_add_remove_ready_queue`: add_task enqueues, next_task dequeues correctly

## Defensive Fixes Verified
- `add_task` resets `in_ready_queue_`/`runq_next_`/`runq_prev_` before enqueue
  (guard against `elf::load` partial TCB init via `MemPool::alloc` without constructor)
- `reap_orphans` dequeues old idle task before cleanup
- `cleanup_test_tasks` drains ready queue via `reset()` (safe for dangling pointers)
- `cleanup_zombies` dequeues READY tasks before freeing
- All 13 raw `state = READY` assignments replaced with `Scheduler::set_task_ready()`