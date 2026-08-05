#!/usr/bin/env python3
"""lldb sync driver: run after 'target create' + 'gdb-remote' in a command file.
Blocks on Continue(); stops at the first foreign-RSP tick."""
import sys

import lldb

OFF_ID = 0x360
OFF_STATE = 0x370
OFF_CTX_RSP = 0x478
OFF_KST = 0x488
OFF_KST_TOP = 0x490
CPU_CURRENT = 0x469520
ADDR_TICKS = 0x46D178
ADDR_LOAD_RSP = 0x474168
ADDR_SAVE_RSP = 0x474170
ADDR_NEXT_ID = 0x2CC318
ADDR_GEN = 0x474158
RMS_ADDR = 0x285B5E


def rd(tgt, addr):
    err = lldb.SBError()
    mem = tgt.ReadMemory(addr, 8, err)
    if err.Fail() or mem is None:
        return 0
    return int.from_bytes(bytes(mem), "little")


def main():
    dbg = lldb.debugger
    dbg.SetAsync(False)
    target = dbg.GetSelectedTarget()
    process = target.GetProcess()
    print("driver: process=%s state=%d" % (process, process.GetState()))
    sys.stdout.flush()
    bp = target.BreakpointCreateByAddress(RMS_ADDR)
    print("driver: breakpoint %d at 0x%x" % (bp.GetID(), RMS_ADDR))
    sys.stdout.flush()

    hits = 0
    while True:
        err = process.Continue()
        if err.Fail():
            print("continue error:", err.GetCString())
            return 2
        state = process.GetState()
        if state != lldb.eStateStopped:
            print("state=%d (stopped=%d); aborting" % (state, lldb.eStateStopped))
            return 3
        hits += 1
        cur = rd(target, CPU_CURRENT)
        if cur == 0:
            continue
        frame = process.GetSelectedThread().GetFrameAtIndex(0)
        rsp = frame.GetSP()
        kst = rd(target, cur + OFF_KST)
        top = rd(target, cur + OFF_KST_TOP)
        if kst == 0 or top == 0:
            continue
        if rsp < kst or rsp >= top:
            tid = rd(target, cur + OFF_ID)
            state = rd(target, cur + OFF_STATE)
            ctx = rd(target, cur + OFF_CTX_RSP)
            print("=== H2-FOREIGN at rate_monotonic_schedule (hit %d) ===" % hits)
            print("cur=0x%x id=%d state=%d" % (cur, tid, state))
            print("rsp=0x%x kstack=[0x%x-0x%x] ctx.rsp=0x%x" % (rsp, kst, top, ctx))
            print("tick=%d load_rsp=0x%x save=0x%x next_id=%d gen=%d" % (
                rd(target, ADDR_TICKS), rd(target, ADDR_LOAD_RSP),
                rd(target, ADDR_SAVE_RSP), rd(target, ADDR_NEXT_ID),
                rd(target, ADDR_GEN)))
            err2 = lldb.SBError()
            mem = target.ReadMemory(rsp, 96, err2)
            if not err2.Fail():
                for i in range(0, 96, 8):
                    print("  [rsp+%2d] = 0x%x" % (i, int.from_bytes(bytes(mem[i:i+8]), "little")))
            open("/tmp/lldb-h2-foreign.txt", "w").write(
                "FOREIGN cur=0x%x id=%d rsp=0x%x kst=0x%x top=0x%x ctx=0x%x tick=%d" %
                (cur, tid, rsp, kst, top, ctx, rd(target, ADDR_TICKS)))
            return 0
        if hits % 200 == 0:
            print("  hit %d tick=%d rsp=0x%x kst=0x%x" % (hits, rd(target, ADDR_TICKS), rsp, kst))
            sys.stdout.flush()


if __name__ == "__main__":
    sys.exit(main())
