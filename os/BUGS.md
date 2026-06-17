# Open Issues

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

### ID: #004 — SYS_yield
- **Description:** No way for a task to cooperatively yield the CPU. Preemption already works, but a `SYS_YIELD` syscall would allow cooperative scheduling for CPU-bound tasks that want to share the core voluntarily.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #005 — SYS_reboot / SYS_halt
- **Description:** No clean way to reboot or power off the system from userspace. A `SYS_REBOOT`/`SYS_HALT` syscall pair is needed for shutdown scripts and init-level power management.
- **Severity:** Recommendation (New Feature)
- **Domain:** General Functional
- **Status:** Open

### ID: #006 — Lock-Free Ring Buffer (ISR→Task Handoff)
- **Description:** ISRs currently block or drop data when the target task's queue is full. A wait-free single-producer/single-consumer ring buffer for ISR-to-task handoff would eliminate priority inversion and data loss in interrupt context.
- **Severity:** Recommendation (New Feature)
- **Domain:** Safety-Critical (ASIL/SIL impact)
- **Status:** Open

## Test Quality Issues

### ID: #007 — test_idle_task output missing from serial
- **Description:** Individual test names from `test_idle_task.cpp` (e.g. `idle_task_created_at_boot`, `idle_task_integrity_markers`) do not appear in the QEMU serial output captured by `make test-qemu`. Only the registration line `Registering idle task tests` is visible. The tests themselves pass (exit code 0) and their symbols exist in the ELF, but their `[PASS]`/`[FAIL]` lines are not emitted or get lost in serial output. Root cause unknown — possibly serial buffer interleaving with terminal escape sequences during test isolation snapshot/restore.
- **Severity:** Low (cosmetic/observability)
- **Domain:** Test Infrastructure
- **Status:** Fixed (tests now appear in output)
