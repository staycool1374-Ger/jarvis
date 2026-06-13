# 0.2.11 Coding Style Refactoring - Remaining TODOs

## ✅ Already Done
- [x] Const correctness retrofit — `const` on all kernel variables, params, member functions
- [x] References over pointers — migrate non-nullable `T*` params to `T&`
- [x] All variables initialized — fix every uninitialized local declaration
- [x] Constructor init-list migration — member assignments in body → member initializer list
- [x] Remove `const_cast` — use `mutable` or redesign to avoid const modification
- [x] Dynamic allocation audit — replace `new`/`delete` on kernel paths with fixed pools
- [x] Validation — zero errors from `make check-style` (exit 0)
- [x] Descriptive names — rename blocklisted single-char vars (`t`, `v`, `val`, `tmp`, `ptr`, `p`)
- [x] Bounded loops — validator now skips intentional blocking I/O loops (hlt, pause, inb, keyboard)

## ✅ Completed
- [x] Meaningful sentinel enums — replaced raw -1 with named constants in module headers
- [x] Doxygen compliance — `@brief`, `@param`, `@return` on all public API function declarations across all 41 kernel headers