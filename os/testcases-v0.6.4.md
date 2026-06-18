# Test Cases — v0.6.4 (Phase 7: Idle-Task Safety Monitors)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### RAM March Test — test_ram_march.cpp
- Idle task executes non-destructive March C- over unused RAM regions
- 0x55 pattern written and verified per address
- 0xAA pattern written and verified per address
- Original data restored byte-for-byte after test
- March test on used memory regions is skipped (no corruption)
- Single-bit error is detected and logged
- Multi-bit error (two cells flipped) is detected and logged
- Error counter exposed via `/proc/health` or `SYS_HEALTH_STATUS`
- Test completes within bounded time per sweep cycle

### CPU Register Verification — test_cpu_verify.cpp
- Idle task executes ALU test: (a + b) - b == a for randomised 64-bit operands
- Carry/overflow flags produce expected results for edge cases (0, MAX_U64, 1)
- Multiplication/division: a * b / b == a (for b ≠ 0)
- MSR read-back test: write known pattern to a scratch MSR, read back matches
- Non-scratch MSRs are read-only verified (write skipped)
- All general-purpose registers (RAX, RBX, RCX, RDX, RSI, RDI, R8–R15) exercised
- Flags register (RFLAGS) verified: set each flag, read back, clear, read back
- Verification failure triggers health counter increment and log entry
- Verification skipped during high system load (no latency impact)