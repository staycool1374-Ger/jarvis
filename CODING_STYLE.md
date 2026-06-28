# Jarvis RTOS Coding Style

## 1. Language & Build

- **Standard**: C++20 freestanding (`-ffreestanding -fno-exceptions -fno-rtti -nostdlib`)
- **Target**: `x86_64-elf`; no STL, exceptions, RTTI, stack-protector, thread-safe statics, dynamic linking on RT paths
- **Optimization**: `-O3` (debug), `-O2` (release)

## 2. Naming

| Category | Convention | Example |
|---|---|---|
| Classes/Structs/Enums | `PascalCase` | `TaskControlBlock` |
| Member/Free functions | `snake_case` | `alloc_page()` |
| Variables/Parameters | `snake_case` | `page_table_` |
| Constants (`constexpr`) | `SCREAMING_SNAKE` | `MAX_BUFFERS` |
| Macros | `SCREAMING_SNAKE` | `ENSURE()` |
| Namespaces | `lowercase` | `kernel`, `arch` |
| Template params | `PascalCase` | `typename T` |
| UDL | `_KiB`, `_MiB` | `16_KiB` |

## 3. File Structure

```cpp
/// @file <path>/<file>.hpp /// @brief <one-line>
#pragma once
#include <types.hpp>             // lib first
#include <constants.hpp>
#include <kernel/<module>/...>   // kernel headers
#include <lib/...>               // then lib
```

- **Guard**: `#pragma once` only
- **Include order**: `<types.hpp>` → `<kernel/...>` → `<lib/...>` → local
- Prefer forward declarations over includes
- No `using namespace` in headers (OK in `.cpp`)

## 4. Memory & Ownership

- No `new`/`delete`/`malloc`/`free` on RT paths — use pools/fixed arrays
- PMM: `alloc_user_page()` (user) vs `alloc_page()` (kernel)
- VMM: `map_page_in_pml4()` for non-default AS
- User memory: must use `CheckedPtr<T>` or `safe_copy_from_user()`; no raw ptr arithmetic

## 5. Error Handling

### 5.1 ENSURE (Fatal — Always On)
```cpp
ENSURE(ptr != nullptr);  // panics with file:line
```
For invariants that must never fail.

### 5.2 ASSERT (Debug Only)
```cpp
ASSERT(pmm::Error::OOM);  // logs via Logger::error()
```
For expected failures (OOM, I/O errors). No-ops in release (`-UCONFIG_DEBUG`).

### 5.3 Module Error Headers
Each module defines error codes in `*_errors.hpp` via X-macro pattern with `error_string<E>()`.

### 5.4 ErrorOr<T>
```cpp
ErrorOr<uint64_t> r = alloc_page();
if (!r.ok()) return;
uint64_t page = *r;
```

## 6. Safety & Compliance

- MISRA C++:2023, AUTOSAR, ISO 26262 ASIL D / IEC 61508
- **Fully bounded loops**: deterministic max iteration; no `while(true)`/`for(;;)` without bound. Exception: blocking I/O loops (`hlt`, `pause`, `inb`, keyboard poll)
- No `volatile` for sync — use atomics or mutexes
- No primitive `reinterpret_cast` — use aligned punning
- No `dynamic_cast`, no `typeid`

## 7. Testing

- Test-first: write stub first, then implement
- Format:
  ```
  // Runmode: kernel
  // Testidea: <what>
  // Input: <setup>
  // Expect: <outcome>
  // Depends: <deps>
  JARVIS_TEST(name) { ... }
  ```
- Stubs: `JARVIS_TEST_PASS()` only
- Registration: `JARVIS_REGISTER_TEST(name)` (debug), `JARVIS_REGISTER_RELEASE_TEST(name)` (release)
- Tests on `testbed` branch, merged to `main`

### 7.1 Test Resource Management

- **Single-owner resources** use `UniquePtr<T, Deleter>` (e.g., `TaskPtr`, `SimpleTaskPtr` from `<kernel/test/task_ptr.hpp>`)
- **Multi-resource cleanup** uses `ScopeGuard` with a lambda
- **Never** `dismiss()` a `ScopeGuard` and manually repeat the same cleanup — let the destructor fire on all exit paths
- Custom deleters live in `src/kernel/test/task_ptr.hpp`

## 8. Formatting

- **Indent**: 4 spaces, no tabs
- **Line length**: 80 chars (90 for comments, data tables excluded)
- **Braces**: same line
- **Spaces**: around binary ops, after `if`/`while`/`for`; none after `(` or before `)`
- Trailing commas in multi-line init lists

## 9. Documentation

Doxygen for public APIs:
```cpp
/// @brief <one-line>
/// @param <name> <desc>
/// @return <desc>
```
File header: `/// @file`, `/// @brief`. Inline comments for complex logic only.

## 10. Kernel-Specific Rules (`src/kernel/` only)

### 10.1 Const Correctness
Everything not modified must be `const` — variables, parameters, member functions.

### 10.2 Prefer References Over Pointers
`T&` for non-nullable params; `T*` only for nullable/optional or output params.
```cpp
// Good
void enqueue_writer(TaskControlBlock& task);
bool find_by_id(uint64_t id, TaskControlBlock* result);
// Bad
void enqueue_writer(TaskControlBlock* task);  // never null — use ref
```

### 10.3 All Variables Must Be Initialized
```cpp
size_t count = 0;                          // Good
Message msg{};                             // Good
size_t count;                              // Bad
```

### 10.4 Constructor Initializer Lists
Member initializer list required — no body assignment.
```cpp
Task::Task(uint64_t id) : id_(id), state_(READY) {}    // Good
Task::Task(uint64_t id) { id_ = id; state_ = READY; }  // Bad
```

### 10.5 Meaningful Sentinel Definitions
Named constants (enums/constexpr) only; no raw magic numbers. **Each sentinel value must be unique across the project** (-1, -2, -3...).
```cpp
enum class BufferSentinel : int64_t { INVALID_HANDLE = -2 };
enum class VfsSentinel : int64_t    { INVALID_FD = -4 };
// Bad: duplicate -1 across enums
```

### 10.6 Descriptive Names — No Single Characters
- Minimum 3 chars; use `<thing>_<instance>` pattern: `msr_low`, `page_idx`, `virt_addr`
- **Blocklist**: `tmp`, `temp`, `ptr`, `p`, `t`, `v`, `val`
- **Allowlist** (abbreviations): `id`, `fd`, `va`, `cs`, `tv`, `vn`, `st`, `en`, `it`, `ok`, `to`, `q`, `n`, `m`, `y`, `c`, `s`, `r`, `lo`, `hi`, `tm`, `h`, `L`, `T`, `O`, `C`
- Loop indices `i`/`j`/`k`/`idx` OK for tight loops (body ≤ 5 lines); not for params or members

### 10.7 const_cast Is Forbidden
Use `mutable` or redesign instead.
