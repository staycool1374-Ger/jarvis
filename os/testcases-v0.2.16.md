# Test Cases — v0.2.16 (CPU Features & RNG)

### FPU/SSE — test_fpu.cpp (existing — 4 tests)
- `fpu_cpuid_detection` — CPUID leaf 1 EDX bits 0 (FPU), 24 (FXSR), 25 (SSE) asserted
- `fpu_basic_instruction` — FINIT clears CR0.TS, sets fpu_owner, marks fpu_used
- `fpu_fxsave_nonzero` — FLDL(1.0) → FXSAVE tag word shows ST0 non-empty
- `fpu_context_switch` — 2-task lazy switch preserves raw IEEE 754 bits (3.14159)

### RNG — test_random.cpp (existing — 7 tests)
- `random_basic_smoke` — 256 bytes not all-zero or all-FF
- `random_not_repeating` — consecutive 64-byte buffers differ
- `random_u64_not_constant` — 3 calls produce varied output
- `random_partial_fills` — odd lengths (1,3,7,33) return non-zero
- `random_large_buffer` — 8192-byte fill completes
- `random_zero_length` — zero-length no-op preserves buffer
- `random_cpuid_detection` — RDRAND/RDSEED CPUID bits (no fault)

### /dev/random VFS — test_random_vfs.cpp (new — 2 tests)
- `dev_random_resolve` — vfs::resolve("/dev/random") returns S_IFCHR vnode
- `dev_random_read` — read 256 bytes via vnode->ops->read, verify non-zero

### SYS_GETRANDOM syscall — test_random_syscall.cpp (new — 4 tests)
- `syscall_getrandom_basic` — 64 bytes via Syscall::handle(GETRANDOM), non-zero
- `syscall_getrandom_zero` — zero-length returns 0, buffer unchanged
- `syscall_getrandom_large` — 4096 bytes via syscall, non-zero
- `syscall_getrandom_invalid_flags` — non-zero flags returns -1

### SSE/XMM context switch — test_fpu_sse.cpp (new — 3 tests)
- `sse_cpuid_detection` — CPUID leaf 1 EDX bit 25 (SSE) + leaf 1 ECX bit 0 (SSE2)
- `sse_mxcsr_preserve` — LDMXCSR(0x1F80) → yield → read MXCSR, verify preserved
- `sse_xmm_context_switch` — 2-task switch with MOVAPS/MOVUPS, verify XMM register content
