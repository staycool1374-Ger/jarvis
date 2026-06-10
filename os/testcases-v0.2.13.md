# Test Cases — v0.2.13 (Phase 3: Hardware Enablement)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### PCI Enumeration — test_pci.cpp
- Scan bus 0, find at least one valid device
- Read vendor/device ID from config space
- Parse capability lists (MSI, MSI-X, PM)
- Allocate BAR resources correctly
- Invalid bus/device/slot returns 0xFFFFFFFF
- PCI header type identifies bridge vs endpoint

### Virtio Transport — test_virtio.cpp
- Detect virtio-mmio or virtio-pci device
- Configure descriptor ring (init, size negotiation)
- Submit and receive descriptor buffers
- Virtio-blk read/write sector round-trip
- Virtio-blk discard (TRIM) operation
- Multiple concurrent descriptor requests
- Queue full condition handled gracefully

### Minimal Network Stack — test_network.cpp
- ARP request/response round-trip
- IPv4 packet send/receive
- UDP datagram send/receive
- Checksum verification (IP, UDP)
- Fragment reassembly (if supported)
- Invalid packet (bad checksum) is dropped

### FPU/SSE Context Switch — test_fpu.cpp
- Lazy FPU restore on context switch
- FXSAVE/FXRSTOR preserves all XMM registers
- FPU state is task-local (no cross-task leakage)
- SSE arithmetic across context switch
- No FPU save for kernel-only tasks (no FPU used)

### Entropy & RNG — test_entropy.cpp
- `/dev/random` returns non-zero bytes
- `/dev/urandom` returns non-zero bytes
- `SYS_GETRANDOM` fills user buffer
- ChaCha20 PRNG produces deterministic output for same seed
- RDRAND/RDSEED instruction availability detected
- Entropy pool depletion blocks /dev/random
