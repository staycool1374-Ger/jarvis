# Test Cases — v0.2.15 (Phase 3: Hardware Enablement)

## Branch: testbed only

### PCI Enumeration — test_pci.cpp (16 tests)
- pci_scan_finds_devices — CF8/CFC config-space access probes vendor/device ID; QEMU PC machine returns > 0 devices
- pci_read_host_bridge_ids — Reads vendor/device ID for 00:00.0; vendor_id != 0xFFFF/0, device_id != 0
- pci_read_host_bridge_info — pci_read_device_info populates all fields; class_code=0x06 (bridge), subclass=0x00 (host bridge)
- pci_finds_isa_bridge — PCI-to-ISA bridge (class=0x06, subclass=0x01) found at 00:01.0
- pci_nonexistent_bdf — Non-existent BDF (255:31.7) returns vendor=0xFFFF, pci_device_exists=false
- pci_read_command_register — Command register at 00:00.0 != 0 (IO/memory space enabled by firmware)
- pci_make_addr_format — pci_make_addr constructs correct CF8 address format
- pci_find_nonexistent_device — pci_find_device(0xFF, 0xFF) returns nullptr
- pci_host_bridge_no_msi — Host bridge 00:00.0 has no MSI capability (cap=0)
- pci_alloc_free_vector — pci_alloc_vector returns valid vector >=48, !=0x80; pci_free_vector releases it
- pci_enable_msi_no_cap — pci_enable_msi on host bridge returns 0 (no MSI capability)
- pci_enable_msix_no_cap — pci_enable_msix on host bridge returns 0 (no MSI-X capability)
- pci_isa_bridge_caps — ISA bridge 00:01.0 has no MSI/MSI-X capability
- pci_bar_registers — At least one device has valid BARs with non-zero address/size and valid type
- pci_msi_capability_chain — Virtio-net device has MSI or MSI-X capability (pci_find_capability returns non-zero)
- pci_enumeration_bounded_time — PCI scan completes in < 1M TSC ticks on QEMU

### Virtio Transport — test_virtio.cpp (9 tests)
- virtio_no_device_found — virtio_find_device(VIRTIO_DEVICE_BLOCK) returns false on default QEMU (no virtio-blk)
- virtio_not_virtio_device — virtio_is_virtio_device returns false for host bridge
- virtio_blk_probe_no_device — VirtioBlkDriver::probe() returns nullptr
- virtio_map_mmio_invalid — virtio_map_mmio returns false for invalid BAR (BAR=6)
- virtio_init_transport_noop — virtio_init_transport returns true on dummy transport (harmless no-op)
- virtio_device_reset — Virtio-net device reset to VIRTIO_STATUS_RESET via status register
- virtio_feature_negotiation — Feature negotiation (VIRTIO_F_VERSION_1) succeeds on virtio-net device
- virtio_queue_configuration — Queue 0 configured with 256 descriptors on virtio-net
- virtio_guest_notify — MMIO notify write to virtio-net queue 0 completes without fault

### DMA Driver — test_dma.cpp (10 tests)
- dma_alloc_buffer — DMA buffer alloc returns valid phys_addr != 0, size >= requested, owned=true, zero-filled
- dma_buffer_write_read — DMA buffer write/read pattern matches
- dma_sg_from_buffer — sg_from_buffer creates single-entry SG list with correct phys/virt/length
- dma_prd_from_sg — PRD table construction: count=1, byte_count=size-1, EOT flag set
- dma_bus_master_host_bridge — pci_set_bus_master on host bridge is harmless no-op
- dma_sg_reset — sg_reset clears count=0, total_length=0
- dma_prd_reset — prd_reset clears count=0
- dma_free_empty_buffer — free_buffer on zero-initialized buffer is no-op
- dma_zero_length_buffer — alloc_buffer(0) returns phys_addr=0 gracefully
- dma_sg_non_contiguous_prd — Two DMA buffers → 2-entry SG → 2-entry PRD with distinct phys_addr

### IDT / Interrupts — test_idt.cpp (6 tests)
- idt_entries_initialized — All 256 IDT entries have non-null handlers after init
- idt_exception_handlers_mapped — CPU exception vectors 0-31 all have valid handlers
- idt_irq_remapped — PIC IRQ0-IRQ15 mapped to vectors 0x20-0x2F
- idt_syscall_handler_installed — Vector 0x80 handler points to syscall dispatch
- idt_double_fault_uses_ist — Double fault (vector 8) IST index set to valid IST stack (1-7)
- idt_reserved_vectors_null — Vectors 0x30-0x7F are null or point to spurious handler

### VFS — test_vfs.cpp (13 tests)
- vfs_fdtable_alloc_free — Single fd alloc/free from FdTable
- vfs_fdtable_multiple — Multiple fd alloc/free with recycle
- vfs_resolve_root — Resolve "/" returns directory vnode
- vfs_resolve_dev — Resolve "/dev" returns directory vnode
- vfs_resolve_tty — Resolve "/dev/tty" returns character device vnode
- vfs_resolve_null — Resolve "/dev/null" returns character device vnode
- vfs_resolve_proc — Resolve "/proc" returns directory vnode
- vfs_resolve_nonexistent — Resolve nonexistent path returns nullptr
- vfs_resolve_relative_path — Resolve "/dev/./tty" and "/dev/../dev/tty" work
- vfs_resolve_dotdot — Resolve "/dev/.." returns root directory
- vfs_mount_unmount — Mount fake filesystem at "/mnt/testfs", resolve returns fake root vnode
- vfs_mkdir_valid/unlink — mkdir/unlink operations on valid/nonexistent/non-dir parents

### Network Stack — test_net.cpp (5 tests)
- net_mac_address_ops — MacAddr equality, broadcast/null detection
- net_ipv4_addr_roundtrip — Ipv4Addr from_u32/as_u32 round-trip; octet access
- net_arp_cache_ops — ArpCache add/lookup/remove/clear operations
- net_ipv4_checksum — IPv4 header checksum calculation and verification
- net_ether_type_swap — __builtin_bswap16 correctly swaps EtherType constants