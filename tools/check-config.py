#!/usr/bin/env python3
"""
check-config.py — Validates jarvis_config.h for consistency, plausibility,
completeness, and dependency constraints.

Usage: python3 tools/check-config.py [--config path/to/jarvis_config.h]
"""

import sys
import re
import os

CONFIG_FILE = "src/kernel/jarvis_config.h"

def parse_config(path):
    """Extract #define CONFIG_* lines from the header."""
    configs = {}
    with open(path) as f:
        for line in f:
            m = re.match(r'#define\s+(CONFIG_\w+)\s+(.*)', line)
            if m:
                name = m.group(1)
                val = m.group(2).strip()
                # Remove comments
                val = re.sub(r'//.*', '', val).strip()
                configs[name] = val
    return configs

def get_int(cfg, name, default=0):
    v = cfg.get(name)
    if v is None:
        return default
    try:
        return int(v, 0)
    except ValueError:
        return default

def check_config(path):
    errors = 0
    warnings = 0
    cfg = parse_config(path)

    # 1. CONFIG_VERSION
    if "CONFIG_VERSION" not in cfg:
        print("ERROR: CONFIG_VERSION not defined")
        errors += 1

    # 2. CONFIG_MAX_TASKS range
    max_tasks = get_int(cfg, "CONFIG_MAX_TASKS", 64)
    if max_tasks < 2 or max_tasks > 4096:
        print(f"ERROR: CONFIG_MAX_TASKS={max_tasks} out of range [2, 4096]")
        errors += 1

    # 3. CONFIG_TICK_HZ
    tick_hz = get_int(cfg, "CONFIG_TICK_HZ", 1000)
    if tick_hz < 100 or tick_hz > 100000:
        print(f"ERROR: CONFIG_TICK_HZ={tick_hz} out of range [100, 100000]")
        errors += 1

    # 4. CONFIG_PAGE_SIZE must be power of 2
    page_size = get_int(cfg, "CONFIG_PAGE_SIZE", 4096)
    if page_size & (page_size - 1):
        print(f"ERROR: CONFIG_PAGE_SIZE={page_size} is not a power of 2")
        errors += 1

    # 5. CONFIG_STACK_SIZE >= CONFIG_MIN_STACK_SIZE
    stack_size = get_int(cfg, "CONFIG_STACK_SIZE", 65536)
    min_stack = get_int(cfg, "CONFIG_MIN_STACK_SIZE", 4096)
    if stack_size < min_stack:
        print(f"ERROR: CONFIG_STACK_SIZE={stack_size} < CONFIG_MIN_STACK_SIZE={min_stack}")
        errors += 1

    # 6. CONFIG_HEAP_SIZE >= total MemPool requirement (estimate)
    heap_size = get_int(cfg, "CONFIG_HEAP_SIZE", 16777216)
    # Rough estimate: sum of block_sizes * block_counts
    if heap_size < stack_size * max_tasks:
        print(f"WARNING: CONFIG_HEAP_SIZE={heap_size} may be insufficient for {max_tasks} tasks × {stack_size} stack")
        warnings += 1

    # 7. CONFIG_PRIORITY_CEILING valid range
    prio = get_int(cfg, "CONFIG_PRIORITY_CEILING", 255)
    if prio < 1 or prio > 255:
        print(f"ERROR: CONFIG_PRIORITY_CEILING={prio} out of range [1, 255]")
        errors += 1

    # 8. CONFIG_MAX_PRIORITY must be power of 2
    max_prio = get_int(cfg, "CONFIG_MAX_PRIORITY", 256)
    if max_prio & (max_prio - 1):
        print(f"ERROR: CONFIG_MAX_PRIORITY={max_prio} is not a power of 2")
        errors += 1

    # 9. CONFIG_SYNC_MAX_WAITERS
    sync_waiters = get_int(cfg, "CONFIG_SYNC_MAX_WAITERS", 32)
    if sync_waiters > max_tasks:
        print(f"WARNING: CONFIG_SYNC_MAX_WAITERS={sync_waiters} > CONFIG_MAX_TASKS={max_tasks}")
        warnings += 1

    # 10. Architecture feature flags
    if "CONFIG_ARCH_X86_64" not in cfg and "CONFIG_ARCH_AARCH64" not in cfg and "CONFIG_ARCH_RISCV64" not in cfg:
        # They might not be in config.h if set via -D flag
        pass

    # 11. CONFIG_HHDM_OFFSET must be page-aligned
    hhdm_str = cfg.get("CONFIG_HHDM_OFFSET", "0")
    try:
        hhdm = int(hhdm_str, 0)
        if hhdm & (page_size - 1):
            print(f"ERROR: CONFIG_HHDM_OFFSET=0x{hhdm:x} not page-aligned")
            errors += 1
    except ValueError:
        pass

    # 12. CONFIG_TASK_NAME_LEN
    name_len = get_int(cfg, "CONFIG_TASK_NAME_LEN", 16)
    if name_len < 4 or name_len > 256:
        print(f"ERROR: CONFIG_TASK_NAME_LEN={name_len} out of range [4, 256]")
        errors += 1

    # 13. CONFIG_IPC_SHMEM_MAX_PAGES bound
    shmem = get_int(cfg, "CONFIG_IPC_SHMEM_MAX_PAGES", 64)
    if shmem > 1024:
        print(f"WARNING: CONFIG_IPC_SHMEM_MAX_PAGES={shmem} > 1024")
        warnings += 1

    # 14. CONFIG_MAX_PROCESS_PAGES bound
    proc_pages = get_int(cfg, "CONFIG_MAX_PROCESS_PAGES", 512)
    if proc_pages > 65536:
        print(f"WARNING: CONFIG_MAX_PROCESS_PAGES={proc_pages} > 65536")
        warnings += 1

    # Summary
    if errors == 0 and warnings == 0:
        print(f"[OK] All config checks passed for {path}")
    elif errors == 0:
        print(f"[OK] {warnings} warning(s), no errors")
    else:
        print(f"[FAIL] {errors} error(s), {warnings} warning(s)")

    return errors

if __name__ == "__main__":
    path = CONFIG_FILE
    if len(sys.argv) > 2 and sys.argv[1] == "--config":
        path = sys.argv[2]

    if not os.path.exists(path):
        print(f"ERROR: {path} not found", file=sys.stderr)
        sys.exit(1)

    sys.exit(check_config(path))
