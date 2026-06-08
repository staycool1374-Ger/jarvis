# Test Coverage Guide

## Current State
- 72 tests, 14 files → ~35% kernel function coverage
- 30 of 37 syscalls untested
- Critical gaps: scheduler cleanup, task lifecycle, sync blocking paths, IPC blocked senders, ELF load/exec

## How to Write a Test

```cpp
// File: src/kernel/test/test_<subsystem>.cpp
#include <test.hpp>

JARVIS_TEST(my_feature) {
    // 1. Set up state
    // 2. Call the function
    // 3. Assert the result
    JARVIS_ASSERT(result == expected_value);
}

void register_<subsystem>_tests() {
    JARVIS_REGISTER_TEST(my_feature);
}
```

- One test per behavior (not per function)
- Add `JARVIS_REGISTER_TEST(name)` in the registration function
- Add `register_<subsystem>_tests()` call in `test_registry.cpp::register_selftest_tests()`
- Include `<test.hpp>`, use `using namespace kernel;`

## Priority Order

### P0 — Scheduler & Task Lifecycle (fix bugs first)
| Test | What to check | File |
|---|---|---|
| `scheduler_remove_task` | After remove_task, current_task() returns valid task, task_count decreases | `test_scheduler.cpp` |
| `scheduler_reap_orphans` | Terminated task without parent gets reaped within 100 ticks; parent-waiting task is NOT reaped | `test_scheduler.cpp` |
| `task_cleanup_frees_resources` | After cleanup(), kernel/user stacks freed, msg_queue/notify/event_group deleted | `test_task.cpp` (new) |
| `task_create_user_page_table` | create_user() produces task with valid page_table_, user_stack_ | `test_task.cpp` (new) |
| `task_clone_shares_page_tables` | Fork child has page_table_shared_=true, parent's pages survive child exit | `test_task.cpp` (new) |

### P1 — IPC Blocked Senders
| Test | What to check | File |
|---|---|---|
| `ipc_send_block_full` | Send to full queue blocks sender; then recv unblocks it | `test_ipc.cpp` |
| `ipc_wake_sender_terminated` | When destination task exits, blocked sender is woken with error (not left BLOCKED forever) | `test_ipc.cpp` |
| `ipc_send_sync_roundtrip` | send_sync blocks, receiver replies, sender unblocks with data | `test_ipc.cpp` |

### P2 — Sync Primitives (blocking paths)
| Test | What to check | File |
|---|---|---|
| `semaphore_wait_post` | Task blocks on semaphore_wait, another task's post unblocks it | `test_sync.cpp` (new) |
| `mutex_lock_unlock` | Task blocks on locked mutex, unlock unblocks it with correct owner | `test_sync.cpp` (new) |
| `queue_send_receive_block` | Send to full queue blocks; receive unblocks; receive from empty blocks | `test_sync.cpp` (new) |

### P3 — ELF Loader
| Test | What to check | File |
|---|---|---|
| `elf_load_invalid_segment` | Segment with bad vaddr/alignment/size returns nullptr | `test_elf.cpp` |
| `elf_load_sets_std_fds` | After load, task has fds 0,1,2 pointing to /dev/tty | `test_elf.cpp` |
| `elf_load_creates_user_stack` | After load, task has user_stack_ at STACK_VADDR | `test_elf.cpp` |

### P4 — Syscall Handlers (30 missing)
| Test | What to check | File |
|---|---|---|
| `syscall_open_read_close` | OPEN returns fd > 2, READ reads bytes, CLOSE frees the fd | `test_syscall.cpp` |
| `syscall_write_fstat` | WRITE returns byte count, FSTAT returns valid stat | `test_syscall.cpp` |
| `syscall_fork_returns_pid` | FORK returns child PID (in test context) | `test_syscall.cpp` |
| `syscall_exec_nonexistent` | EXEC of missing file returns -1 | `test_syscall.cpp` |
| `syscall_pipe_read_write` | PIPE creates two fds, write to pipe[1] readable from pipe[0] | `test_syscall.cpp` |
| `syscall_signal_sigreturn` | SIGNAL sets handler, SIGRETURN restores context | `test_syscall.cpp` |
| `syscall_send_recv` | SEND to another task, RECV gets the message | `test_syscall.cpp` |
| `syscall_chdir_getcwd` | CHDIR changes directory, GETCWD returns it | `test_syscall.cpp` |

### P5 — VFS Edge Cases
| Test | What to check | File |
|---|---|---|
| `vfs_resolve_relative_path` | resolve("dev/tty") from root cwd returns same as resolve("/dev/tty") | `test_vfs.cpp` |
| `vfs_resolve_dotdot` | resolve("/dev/../dev/tty") works | `test_vfs.cpp` |
| `vfs_mount_unmount` | mount/unmount updates mount table, resolve reflects changes | `test_vfs.cpp` |

## Assertion Patterns

```cpp
// Equality
JARVIS_ASSERT_EQ(42, result);
JARVIS_ASSERT_HEX_EQ(0xABC, hex_val);

// Null/boolean
JARVIS_ASSERT(ptr != nullptr);
JARVIS_ASSERT(ptr == nullptr);
JARVIS_ASSERT(condition == true);

// Range/state checks
JARVIS_ASSERT(count >= 0);
JARVIS_ASSERT(task->state == TaskState::READY);
```

## Running Tests
```bash
make test-qemu          # Run all 72+ tests
make profiling          # Build + run + generate coverage report
# Open build/profiling/report/index.html
```

## Target: 80% coverage
- Write P0 tests first (fix known bugs)
- Then P1–P4
- Aim for 200+ tests total
- Each test should be < 20 lines
