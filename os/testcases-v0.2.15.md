# Test Cases — v0.2.15 (Phase 3: Hardware Enablement)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### PCI Enumeration — test_pci_enum.cpp
- CF8/CFC config-space access probes vendor/device ID
- Bridges are detected and subordinate buses enumerated
- BAR registers are read and decoded (IO vs MMIO, 32-bit vs 64-bit)
- MSI/MSI-X capability detected and capability chain traversed
- PCI device tree printed via debug output
- Enumeration completes within bounded time

### Virtio Transport — test_virtio.cpp
- Virtio PCI capability (vendor 0x1AF4) detected
- Virtio device reset completes successfully
- Feature negotiation (host → guest) succeeds
- Queue configuration (descriptor, avail, used rings) set up correctly
- Guest notifies device via MMIO write

### DMA Driver — test_dma.cpp
- Scatter-gather PRD table constructed from non-contiguous physical pages
- DMA transfer completes with correct data in destination buffer
- Ring-buffer management wraps without data loss
- DMA completion interrupt fires and is acknowledged
- Double-buffered transfer: next PRD chain loaded while current runs
- Transfer of zero-length buffer returns immediately (no DMA start)

### Network Stack (ARP) — test_arp.cpp
- ARP request sent for gateway IP
- ARP reply received and MAC cached
- Duplicate ARP request updates cache
- Cache entry expires after configured timeout
- ARP for local IP responds immediately

### Network Stack (IPv4/UDP) — test_udp.cpp
- UDP socket created, bound to port
- Packet sent to loopback IP is received
- UDP checksum computed and verified
- Payload received matches payload sent
- Port exhaustion returns EADDRINUSE
- Receive on unbound socket returns ENOTCONN