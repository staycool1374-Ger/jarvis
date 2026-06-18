# Test Cases — v0.5.3 (Phase 6: Userspace Library & Toolchain)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### printf / scanf — test_libc_stdio.cpp
- `printf("hello")` outputs correct string to stdout
- `printf` with format specifiers (%d, %x, %s, %c, %p) produces expected output
- `snprintf` respects buffer size limit
- `fprintf` writes to specified file descriptor
- `scanf` parses integers, hex, strings from input buffer
- `sscanf` with mismatched specifier returns 0 matches

### malloc / free — test_libc_malloc.cpp
- `malloc(n)` returns aligned, non-overlapping blocks
- `free(p)` returns memory to pool
- `malloc(0)` returns valid pointer (or NULL, per implementation)
- `free(NULL)` is a no-op
- `realloc(p, n)` preserves existing content up to min sizes
- `calloc(n, s)` zeroes allocated memory
- Repeated alloc/free cycle does not leak memory
- OOM returns NULL

### String & Memory — test_libc_string.cpp
- `memcpy`, `memmove`, `memset`, `memcmp` produce correct results for all sizes 0..256
- Overlapping regions handled correctly by `memmove`
- `strlen`, `strcpy`, `strncpy`, `strcat`, `strcmp`, `strncmp` match reference libc
- `strchr`, `strrchr`, `strstr`, `strtok` behave as specified

### Threads & Atomics — test_libc_thread.cpp
- `errno` is thread-local (independent values in concurrent tasks)
- Atomic builtins (`__sync_fetch_and_add`, etc.) compile and produce correct results
- `longjmp`/`setjmp` round-trips correctly

### Libc Integration — test_libc_syscall.cpp
- `printf` internally uses `write` syscall (not raw inline assembly)
- `malloc` calls `SYS_BRK` (or equivalent) for heap expansion
- Libc functions do not corrupt kernel-space memory
- Existing userspace programs (shell, ls, cat) link against new libc without modification
- Binary size increase from libc is within acceptable bounds