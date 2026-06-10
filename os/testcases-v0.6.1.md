# Test Cases — v0.6.1 (Phase 7: Hardware Watchdog)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Watchdog Init — test_watchdog_init.cpp
- ICH9 watchdog MMIO region detected
- HPET watchdog capability detected
- Watchdog timer initialized with default timeout (10s)
- Watchdog is not running immediately after init

### NMI Handler — test_watchdog_nmi.cpp
- NMI pre-timeout handler installed
- NMI fires at TCO_RLD/2 (half of timeout)
- NMI handler logs warning and kicks watchdog
- NMI handler does not panic

### Watchdog Kick — test_watchdog_kick.cpp
- `SYS_WATCHDOG_KICK` resets the timer
- Kick before timeout prevents system reset
- Missing kick causes watchdog reset (in QEMU, verified by exit code)
- Invalid kick argument returns EINVAL

### PIT Fallback — test_watchdog_pit.cpp
- When no hardware watchdog, PIT-based software watchdog used
- PIT watchdog timeout matches requested period
- Software watchdog kick works identically
- Mixed hardware+software: hardware preferred
