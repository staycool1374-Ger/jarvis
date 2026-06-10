# Test Cases — v0.5.2 (Phase 6: Safety Hardening & Release)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Stack Cookies — test_stack_cookie.cpp
- `-fstack-protector` enabled for kernel build
- Stack cookie initialized at task creation
- Buffer overflow triggers stack smashing handler
- Stack smashing handler halts or logs and aborts
- No false positives on valid code paths

### Pointer Isolation Audit — test_pointer_isolation.cpp
- No raw `reinterpret_cast` in kernel core (verified by grep/pattern)
- All kernel-user boundary crossings use CheckedPtr
- CheckedPtr validates user-range before dereference
- Kernel pointer leaked to userspace detected and blocked

### Release Build — test_release_build.cpp
- `make release` compiles without errors
- Release build uses `-O3`, `-DNDEBUG`
- Release binary is stripped of symbols
- Release binary runs all release tests (`JARVIS_REGISTER_RELEASE_TEST`)
- Release binary boots in QEMU without debug output

### Doxygen Documentation — test_doxygen.cpp
- Doxygen config parses without warnings
- All public APIs have `///` documentation blocks
- Doxygen output generates clean HTML
