# Test Cases — v0.5.1 (Phase 6: System Integration Testing)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Cross-Boundary Flows — test_cross_boundary.cpp
- Kernel-to-userspace IPC round-trip with payload
- Userspace driver (iocd) delivers keyboard input to shell via IPC
- VFS daemon (vfsd) services open/read/write from userspace
- Capability-gated MMIO access from iocd succeeds
- Unauthorized capability access returns error

### 24-Hour Stress Test — test_stress_24h.cpp
- Sustained fork/exit cycle (10 tasks per second)
- Continuous IPC traffic (100 msg/s between task pairs)
- Memory alloc/free churn (all PMM pages cycled hourly)
- Scheduler invariant checks pass at 1h, 6h, 12h, 24h
- Deadline overrun rate < 0.01%

### IPC Throughput Benchmark — test_ipc_throughput.cpp
- Messages per second between two kernel tasks (single-core)
- Messages per second across kernel-userspace boundary
- Throughput vs payload size (1B, 64B, 256B, 1KB)
- Throughput vs queue depth (1, 4, 16, 64)
- Context switches per second under IPC load

### Context Switch Benchmark — test_cs_bench.cpp
- Task-to-task context switch latency
- Kernel-to-userspace entry/exit latency
- IRQ entry/exit latency
- Syscall entry/exit latency
- All benchmarks publish min/max/avg/p99
