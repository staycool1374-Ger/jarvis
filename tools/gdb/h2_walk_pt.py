#!/usr/bin/env python3
"""lldb driver: locate the harness TCB, walk its kslot stack page tables to get
the phys base, and print the HHDM alias range.  Also reports the current RSP.
Run via:  lldb -b -s h2_walk_pt.txt"""
import sys

import lldb

CPU_CTX = 0xFFFF800000476B20          # kernel::current_cpu()::cpu (CpuContext)
OFF_ID = 0x360                        # TCB.id
OFF_KST = 0x488                       # TCB.kernel_stack
OFF_KST_TOP = 0x490                   # TCB.kernel_stack_top
HHDM = 0xFFFF800000000000
PAGE = 0x1000


def rd(tgt, addr):
    proc = tgt.GetProcess()
    err = lldb.SBError()
    mem = proc.ReadMemory(addr, 8, err)
    if err.Fail() or mem is None:
        return 0
    return int.from_bytes(bytes(mem), "little")


def walk4(tgt, cr3, va):
    """4-level page-table walk of VA using direct-map reads of phys tables."""
    lvl = []
    ent = cr3 & 0xFFFFFFFFFF000
    for shift, masksz in ((39, 0x1FF), (30, 0x1FF), (21, 0x1FF), (12, 0x1FF)):
        idx = (va >> shift) & masksz
        e = rd(tgt, HHDM + ent + idx * 8)
        lvl.append((shift, idx, e))
        if not (e & 1):
            return lvl, 0  # not present
        ent = e & 0xFFFFFFFFFF000
    return lvl, ent


def main():
    dbg = lldb.debugger
    dbg.SetAsync(False)
    target = dbg.GetSelectedTarget()
    process = target.GetProcess()
    print("driver: connected state=%d" % process.GetState())
    sys.stdout.flush()

    # Break on every scheduler tick so Continue() returns periodically.
    bp = target.BreakpointCreateByName('rate_monotonic_schedule')
    if bp is None or bp.GetNumLocations() == 0:
        print("no rate_monotonic_schedule symbol")
        return 4
    print("tick breakpoint id=%d locs=%d" % (bp.GetID(), bp.GetNumLocations()))
    sys.stdout.flush()

    # Continue until the harness (id 1) is current, then locate its TCB.
    cur = 0
    for _ in range(4000):
        err = process.Continue()
        if err.Fail():
            print("continue error:", err.GetCString())
            return 2
        cur = rd(target, CPU_CTX)
        if cur == 0:
            continue
        tid = rd(target, cur + OFF_ID)
        if tid == 1:
            break
    if cur == 0:
        print("harness not found")
        return 3
    print("harness TCB at 0x%x" % cur)
    sys.stdout.flush()

    kst = rd(target, cur + OFF_KST)
    ktop = rd(target, cur + OFF_KST_TOP)
    print("harness kernel_stack=[0x%x-0x%x]" % (kst, ktop))

    frame = process.GetSelectedThread().GetFrameAtIndex(0)
    regs = frame.GetRegisters()
    cr3 = 0
    for rg in regs:
        if rg.GetName() == 'cr3':
            cr3 = rg.GetValueAsUnsigned()
    if cr3 == 0:
        # Fall back to the kernel's PML4 global (set at boot by the VMM).
        cr3 = rd(target, 0xFFFF800000471D10)
    print("CR3=0x%x" % cr3)
    sys.stdout.flush()

    # Walk the FIRST page of the kslot stack.
    lvl, phys = walk4(target, cr3, kst)
    print("walk of 0x%x:" % kst)
    for shift, idx, e in lvl:
        print("  L%d idx=%d entry=0x%x present=%d" % (shift // 9, idx, e, 1 if e & 1 else 0))
    if phys:
        print("phys base of kslot stack = 0x%x  -> HHDM alias = 0x%x" % (phys, HHDM + phys))
        print("HHDM alias stack range = [0x%x-0x%x]" % (HHDM + phys, HHDM + phys + (ktop - kst)))

    # Report current RSP.
    rsp = frame.GetSP()
    print("current RSP=0x%x (frame0: %s)" % (rsp, frame.GetFunctionName() or "?"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
