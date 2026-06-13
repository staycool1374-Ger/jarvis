# 0.2.11 Coding Style Refactoring - Remaining TODOs

## ✅ Already Done
- [x] Const correctness retrofit — `const` on all kernel variables, params, member functions
- [x] References over pointers — migrate non-nullable `T*` params to `T&`
- [x] All variables initialized — fix every uninitialized local declaration
- [x] Constructor init-list migration — member assignments in body → member initializer list
- [x] Remove `const_cast` — use `mutable` or redesign to avoid const modification
- [x] Dynamic allocation audit — replace `new`/`delete` on kernel paths with fixed pools
- [x] Validation — zero errors from `make check-style` (exit 0)

## ⚠️ Remaining Items

### Meaningful sentinel enums — replace raw `-1` checks with named constants
- Found in multiple files (e.g., return -1 for error)

### Descriptive names — rename blocklisted single-char vars (`t`, `v`, `val`, `tmp`, `ptr`, `p`)
- `t` used for TaskControlBlock* in many files
- `v` used for various values
- `p` used for pointers
- Found in: elf.cpp, syscall handlers, scheduler.cpp, procfs.cpp, kernel.cpp, shell.cpp, etc.

### Bounded loops — replace unbounded `while (true)`/`for (;;)` with max-iteration guards
Found in:
- `src/lib/new.cpp:35` - `while (1) { arch::hlt(); }`
- `src/services/shell.cpp:28` - `while (true) arch::hlt();`
- `src/services/shell.cpp:138` - `for (;;) {`
- `src/services/shell.cpp:188` - `while (true) {`
- `src/services/terminal/terminal.cpp:84` - `while (true) {`
- `src/lib/cxxabi.cpp:17,21,25,32` - `while (1) {}`

### Documentation Doxygen compliance — `@brief`, `@param`, `@return` on all public APIs
- Many functions missing Doxygen comments

## Files to Review
- `src/kernel/elf/elf.cpp`
- `src/kernel/syscall/syscall_handlers_misc.cpp`
- `src/kernel/syscall/syscall_handlers_fs.cpp`
- `src/kernel/syscall/syscall_handlers_sync.cpp`
- `src/kernel/syscall/syscall.cpp`
- `src/kernel/syscall/syscall_handlers_process.cpp`
- `src/kernel/task/scheduler.cpp`
- `src/lib/logger.cpp`
- `src/lib/new.cpp`
- `src/lib/test.cpp`
- `src/initrd/initrd.cpp`
- `src/lib/cxxabi.cpp`
- `src/kernel/memory/vmm.cpp`
- `src/kernel/bootparams.cpp`
- `src/kernel/vfs/procfs.cpp`
- `src/kernel/kernel.cpp`
- `src/services/shell.cpp`
- `src/kernel/test/test_idle_task.cpp`
- `src/kernel/test/test_waitpid.cpp`
- `src/kernel/test/test_memory.cpp`
- `src/kernel/memory/mempool.cpp`
- `src/kernel/test/test_pmm.cpp`
- `src/kernel/driver/driver.cpp`
- `src/kernel/test/test_elf.cpp`
- `src/programs/demo/demo.cpp`
- `src/services/terminal/terminal.cpp`
- `src/lib/cxxabi.cpp`

## Next Steps
1. Add max-iteration guards to unbounded loops
2. Replace single-char variable names with descriptive names
3. Add Doxygen comments to public APIs
4. Replace raw -1 sentinel values with named constants