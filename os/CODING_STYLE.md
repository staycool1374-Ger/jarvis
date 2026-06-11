# Jarvis RTOS Coding Style

## 1. Language & Build

- **Standard**: C++20 freestanding (`-ffreestanding -fno-exceptions -fno-rtti -nostdlib`)
- **Target**: `x86_64-elf`
- **Optimization**: `-O3` (debug), `-O2` (release)
- **No STL**, no exceptions, no RTTI, no stack-protector, no thread-safe statics
- **No dynamic linking** on real-time paths

## 2. Naming Conventions

| Category | Convention | Example |
|----------|-----------|---------|
| Classes / Structs / Enums | `PascalCase` | `TaskControlBlock`, `BufferPool` |
| Member functions | `snake_case` | `alloc_page()`, `map_page()` |
| Free functions | `snake_case` | `init_task_common()`, `reap_orphans()` |
| Variables / Parameters | `snake_case` | `page_table_`, `kernel_stack` |
| Constants (`constexpr`) | `SCREAMING_SNAKE` | `MAX_BUFFERS`, `PAGE_SIZE` |
| Macros | `SCREAMING_SNAKE` | `ENSURE()`, `JARVIS_ASSERT()` |
| Namespaces | `lowercase` | `kernel`, `arch`, `vfs` |
| Template parameters | `PascalCase` | `typename T`, `typename E` |
| User-defined literals | `_KiB`, `_MiB` | `16_KiB`, `1_MiB` |

## 3. File Structure

```
/// @file <path>/<file>.hpp
/// @brief <one-line description>

#pragma once

#include <types.hpp>             // lib headers first
#include <constants.hpp>
#include <kernel/<module>/...>   // kernel headers
#include <lib/...>

namespace kernel {

// Forward declarations
struct TaskControlBlock;
class BufferPool;

// Code ...

} // namespace kernel
```

- **Include guard**: Always `#pragma once` (no `#ifndef` guards)
- **Include order**: `<lib/types.hpp>` → `<kernel/...>` → `<lib/...>` → local
- **Forward declarations** preferred over including headers where possible
- No `using namespace` in headers; OK in `.cpp` files (`using namespace kernel;`)

## 4. Memory & Ownership

- **No dynamic allocation** (`new`/`delete`/`malloc`/`free`) on real-time paths
- Object pools and fixed-size arrays preferred (e.g., `BufferPool::MAX_BUFFERS = 1024`)
- PMM ownership tracking: `alloc_user_page()` vs `alloc_page()` (kernel)
- VMM page-table management: `map_page_in_pml4()` for non-default address spaces
- **All user-space memory access** must go through `CheckedPtr<T>` or `safe_copy_from_user()`
- No raw pointer arithmetic for user-space addresses

## 5. Error Handling

### 5.1 ENSURE (Fatal Invariant — Always Active)

```cpp
ENSURE(ptr != nullptr);       // never fails — panics with file:line
ENSURE(index < table_size);    // bounds check on fixed-size tables
```

- Use for conditions that **must never fail** (internal logic errors, invariants)
- Always active regardless of build target

### 5.2 ASSERT (Expected Error — Debug Only)

```cpp
ASSERT(pmm::Error::OOM);  // logs [file:line] error via Logger::error()
```

- Use for expected failure paths (OOM, resource exhaustion, I/O errors)
- Compiles to `((void)0)` in release (`-UCONFIG_DEBUG`)

### 5.3 Module Error Headers

- Each module defines error codes in `*_errors.hpp` (e.g., `task_errors.hpp`)
- Use X-macro pattern and specialize `kernel::errors::error_string<E>()`

```cpp
// task_errors.hpp
#define TASK_ERROR_CODES \
    X(OK,          0, "OK") \
    X(OOM,         1, "Task creation failed, out of memory") \
    X(TABLE_FULL,  2, "Maximum of MAX_TASKS reached")

enum TaskError : uint64_t {
#define X(name, num, msg) TASK_ERR_##name = num,
    TASK_ERROR_CODES
#undef X
};

template<>
inline const char* error_string(TaskError e) {
    switch (e) {
#define X(name, num, msg) case TASK_ERR_##name: return msg;
    TASK_ERROR_CODES
#undef X
    }
    return "Unknown task error";
}
```

### 5.4 ErrorOr<T>

```cpp
ErrorOr<uint64_t> result = alloc_page();
if (!result.ok()) { /* handle error */ }
uint64_t page = *result;
```

## 6. Safety & Compliance

- **MISRA C++:2023** and **AUTOSAR** guidelines
- **ISO 26262 ASIL D** / **IEC 61508** functional safety
- **Fully bounded loops**: every loop must have a deterministic maximum iteration count. No `while(true)` or `for(;;)` without an explicit bound
- **No `volatile`** for synchronization; use atomic operations or mutexes
- **No primitive `reinterpret_cast`**: use strongly-typed, alignment-compliant punning
- **Common Criteria isolation**: Ring 0 (kernel) vs Ring 3 (user), complete mediation on VFS pathways
- **No dynamic_cast, no typeid**

## 7. Testing Conventions

- **Test-first**: Write stub tests first, then implement
- **Doc-block format** for every test:

```
// Runmode: kernel
// Testidea: <what is being verified>
// Input: <test inputs and setup>
// Expect: <expected outcome>
// Depends: <list of dependencies>
JARVIS_TEST(test_name) { ... }
```

- **Stubs** use `JARVIS_TEST_PASS()` only — no logic in stubs
- **Registration**: `JARVIS_REGISTER_TEST(name)` (debug-only). Release-computational tests use `JARVIS_REGISTER_RELEASE_TEST(name)`
- Tests are developed on `testbed` branch, merged to `main` after verification

## 8. Formatting

- **Indent**: 4 spaces, no tabs
- **Line length**: 100 characters max
- **Braces**: on same line for functions, control structures, classes

```cpp
void my_function() {
    if (condition) {
        do_something();
    } else {
        do_other();
    }
}
```

- **Spaces** around binary operators (`+`, `-`, `=`, `==`, `&&`, `||`), after keywords (`if `, `while `, `for `), no space after `(` or before `)`
- **Trailing commas** in multi-line initializer lists

## 9. Documentation

- **Doxygen** style for all public APIs:

```cpp
/// @brief <one-line summary>
/// @param <name> <description>
/// @return <description>
/// @note <optional additional info>
```

- File header: `/// @file`, `/// @brief`
- Inline comments for complex logic only

## 10. Kernel-Specific Mandatory Rules

**These rules apply only to `src/kernel/` source files (not tests, not `src/lib/`, not `src/libc/`).**

### 10.1 Const Correctness

- All variables, parameters, and member functions **must be `const`** where the value/state is not modified
- Member functions that do not mutate the object **must** be declared `const`
- Parameters that are not modified **must** be `const`

```cpp
// Good
const size_t count = get_count();
uint64_t lookup(const char* name) const;

// Bad
size_t count = get_count();   // not modified — should be const
uint64_t lookup(char* name);  // not modified — should be const char*
```

### 10.2 Prefer References Over Pointers

- Use `T&` or `const T&` for non-nullable parameters
- Use `T*` only for nullable/optional parameters (can be `nullptr`) or output parameters where the caller expects nullptr

```cpp
// Good
void process(const TaskControlBlock& task, Error* out_error);
bool find_by_id(uint64_t id, TaskControlBlock* result);

// Bad
void process(const TaskControlBlock* task);  // should be ref — never null
```

### 10.3 All Variables Must Be Initialized

- Every local variable must have an explicit initializer at declaration
- Zero-init with `= {}` or explicit value
- Exception: parameters, `extern` declarations

```cpp
// Good
size_t count = 0;
uint64_t flags = PAGE_PRESENT | PAGE_WRITE;
TaskControlBlock* tcb = nullptr;
Message msg{};

// Bad
size_t count;           // uninitialized
uint64_t flags;         // uninitialized
```

### 10.4 Constructor Initializer Lists

- All member initialization must use the **member initializer list**, not assignment in the constructor body

```cpp
// Good
TaskControlBlock::TaskControlBlock(uint64_t id)
    : id_(id), state_(TaskState::READY), kernel_stack_(nullptr) {}

// Bad
TaskControlBlock::TaskControlBlock(uint64_t id) {
    id_ = id;
    state_ = TaskState::READY;
    kernel_stack_ = nullptr;
}
```

### 10.5 Meaningful Sentinel Definitions

- Empty, invalid, or sentinel states must use named constants (enums, constexpr), not raw magic numbers
- The named constant must be used consistently by both the state definition and the checking function

```cpp
// Good
enum BufListHead : int32_t {
    BUF_LIST_EMPTY = -1,
};

bool has_buffers() const {
    return buf_list_head != BUF_LIST_EMPTY;
}

// Bad
int32_t buf_list_head = -1;
bool has_buffers() const { return buf_list_head != -1; }
```

### 10.6 Descriptive Names — No Single Characters

- All variables and parameters must use descriptive names (minimum 3 characters)
- **Blocklist**: `t`, `v`, `val`, `tmp`, `temp`, `ptr`, `p`
- **Exception**: Loop indices `i`, `j`, `k` are allowed only for tight loops (body ≤ 5 lines) where the index scope is limited to the loop
- Exception does not apply to parameters or member variables

```cpp
// Good
for (size_t i = 0; i < MAX_ENTRIES; ++i) { entries[i] = 0; }
size_t buffer_count = 0;
void set_task(TaskControlBlock* actual_task);

// Bad
void set_task(TaskControlBlock* t);          // single char
size_t val = 0;                              // blocklisted
for (size_t idx = 0; idx < n; ++idx) {}      // OK but unusual
```

### 10.7 const_cast Is Forbidden

- `const_cast` must never appear in kernel source code
- Use `mutable` for caching or `const_cast`-free patterns

```cpp
// Forbidden
char* writable = const_cast<char*>(readonly_str);

// Allowed alternatives
mutable uint64_t cached_value_;
// Or redesign to avoid the need for const_cast
```