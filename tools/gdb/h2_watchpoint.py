#!/usr/bin/env python3
"""lldb: hardware-watchpoint on the harness's context.rsp field.
Catches EVERY write to it; reports any foreign value (the displacement save).
Run via: lldb -b -s <this>.txt  (after target create + gdb-remote)."""
import sys

import lldb

OFF_CTX_RSP = 0x478
OFF_KST = 0x488
OFF_KST_TOP = 0x490
CPU_CURRENT = 0x469520
ADDR_TICKS = 0x46D178


def rd(tgt, addr):
    proc = tgt.GetProcess()
    err = lldb.SBError()
    mem = proc.ReadMemory(addr, 8, err)
    if err.Fail() or mem is None:
        return 0
    return int.from_bytes(bytes(mem), "little")


def main():
    dbg = lldb.debugger
    dbg.SetAsync(False)
    target = dbg.GetSelectedTarget()
    process = target.GetProcess()
    print("driver: connected, state=%d" % process.GetState())
    sys.stdout.flush()

    # Continue until the harness (id 1) is current, then locate its TCB.
    while True:
        err = process.Continue()
        if err.Fail():
            print("continue error:", err.GetCString())
            return 2
        cur = rd(target, CPU_CURRENT)
        if cur == 0:
            continue
        tid = rd(target, cur + 0x360)
        if tid == 1:
            break
    print("harness TCB at 0x%x (tick=%d)" % (cur, rd(target, ADDR_TICKS)))
    ctx_addr = cur + OFF_CTX_RSP
    kst = rd(target, cur + OFF_KST)
    top = rd(target, cur + OFF_KST_TOP)
    print("&context.rsp = 0x%x  kstack=[0x%x-0x%x]" % (ctx_addr, kst, top))
    sys.stdout.flush()

    # Hardware watchpoint (write) on the context.rsp field.
    wp = target.WatchpointCreateByAddress(ctx_addr, 8, lldb.eWatchpointWrite, None)
    print("watchpoint id:", wp.GetID())
    sys.stdout.flush()

    hits = 0
    while True:
        err = process.Continue()
        if err.Fail():
            print("continue error:", err.GetCString())
            return 2
        state = process.GetState()
        if state != lldb.eStateStopped:
            print("state=%d aborting" % state)
            return 3
        hits += 1
        val = rd(target, ctx_addr)
        foreign = (val < kst or val >= top)
        frame = process.GetSelectedThread().GetFrameAtIndex(0)
        rip = frame.GetPC()
        print("  wp#%d tick=%d ctx.rsp=0x%x %s rip=0x%x" % (
            hits, rd(target, ADDR_TICKS), val, "FOREIGN!" if foreign else "ok", rip))
        sys.stdout.flush()
        if foreign:
            print("=== FOREIGN context.rsp write (displacement) ===")
            print("current rip=0x%x (write site)" % rip)
            open("/tmp/lldb-wp-foreign.txt", "w").write(
                "FOREIGN ctx.rsp=0x%x rip=0x%x tick=%d" % (val, rip, rd(target, ADDR_TICKS)))
            # Dump backtrace at the write site.
            for i in range(8):
                f = process.GetSelectedThread().GetFrameAtIndex(i)
                if not f.IsValid():
                    break
                print("  #%d 0x%x %s" % (i, f.GetPC(), f.GetFunctionName() or ""))
            return 0
        if hits > 400:
            print("too many hits; giving up (tick=%d)" % rd(target, ADDR_TICKS))
            return 4


if __name__ == "__main__":
    sys.exit(main())
