# Bug & Feature Tracking

## Recommendations (Future Features)

### ID: #001 — AHCI/SATA Driver
- **Description:** The FAT32 filesystem (v0.2.9) targets ATA PIO on QEMU's emulated IDE controller. Real hardware uses AHCI/SATA with NCQ. An AHCI driver is needed for bare-metal storage access.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #002 — Networking Stack (TCP/IP)
- **Description:** The OS currently has no networking capability. A full networking stack (ARP, IP, ICMP, UDP, TCP) with an Ethernet driver is required for distributed real-time communication, remote logging, and networked control systems.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #003 — USB Stack
- **Description:** The OS currently uses PS/2 for keyboard input. Modern hardware relies on USB (UHCI/EHCI/xHCI). A USB stack is needed to support USB keyboards, mice, and storage on real machines.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open
