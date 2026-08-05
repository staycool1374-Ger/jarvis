#!/usr/bin/env python3
"""lldb driver: catch the first foreign-RSP moment in the deterministic replay."""
import sys

import lldb

CPU_CURRENT = 0x469520
ADDR_TICKS = 0x46D178
ADDR_NEST = 0x474140
ADDR_LOAD_RSP = 0x474168
ADDR_SAVE_RSP = 0x474170
ADDR_NEXT_ID = 0x2CC318
ADDR_GEN = 0x474158
RMS_ADDR = 0x285B5E

OFF_ID = 0x360
OFF_STATE = 0x370
OFF_CTX_RSP = 0x478
OFF_KST = 0x488
OFF_KST_TOP = 0x490


def rd(debugger, addr):
    err = lldb.SBError()
    mem = debugger.GetSelectedTarget().ReadMemory(addr, 8, err)
    if err.Fail() or mem is None:
        return 0
    return int.from_bytes(bytes(mem), "little")


def main():
    debugger = lldb.SBDebugger.Create()
    debugger.SetAsync(False)
    target = debugger.CreateTarget("build/kernel-debug.elf")
    err = lldb.SBError()
    process = target.ConnectRemote(
        debugger.GetListener(), "gdb-remote://localhost:1234",
        "gdb-remote", err)
    if err.Fail():
        print("connect failed:", err.GetCString())
        return 1
    print("connected; setting breakpoint at rate_monotonic_schedule 0x%x" % RMS_ADDR)
    bp = target.BreakpointCreateByAddress(RMS_ADDR)
    print("bp id:", bp.GetID())

    hits = 0
    while True:
        process.Continue()
        state = process.GetState()
        if state != lldb.eStateStopped:
            print("state=%d (not stopped); aborting" % state)
            return 2
        hits += 1
        cur = rd(debugger, CPU_CURRENT)
        if cur == 0:
            continue
        rsp = process.GetSelectedThread().GetFrameAtIndex(0).GetSP()
        kst = rd(debugger, cur + OFF_KST)
        top = rd(debugger, cur + OFF_KST_TOP)
        if kst == 0 or top == 0:
            continue
        if rsp < kst or rsp >= top:
            tid = rd(debugger, cur + OFF_ID)
            state = rd(debugger, cur + OFF_STATE)
            ctx = rd(debugger, cur + OFF_CTX_RSP)
            print("\n=== H2-FOREIGN at rate_monotonic_schedule (hit %d) ===" % hits)
            print("cur=0x%x id=%d state=%d" % (cur, tid, state))
            print("rsp=0x%x kstack=[0x%x-0x%x] ctx.rsp=0x%x" % (rsp, kst, top, ctx))
            print("tick=%d nesting=%d" % (rd(debugger, ADDR_TICKS), rd(debugger, ADDR_NEST)))
            print("load_rsp=0x%x save_rsp_to=0x%x next_id=%d gen=%d" % (
                rd(debugger, ADDR_LOAD_RSP), rd(debugger, ADDR_SAVE_RSP),
                rd(debugger, ADDR_NEXT_ID), rd(debugger, ADDR_GEN)))
            print("--- memory at foreign rsp ---")
            err2 = lldb.SBError()
            mem = target.ReadMemory(rsp, 96, err2)
            if not err2.Fail():
                for i in range(0, 96, 8):
                    print("  [rsp+%2d] = 0x%x" % (i, int.from_bytes(bytes(mem[i:i+8]), "little")))
            print("--- registers ---")
            regs = process.GetSelectedThread().GetFrameAtIndex(0).GetRegisters()
            for reg in regs:
                for r in reg:
                    if r.GetName() in ("rsp", "rip", "rax", "rbx", "cr3"):
                        print("  %s = 0x%x" % (r.GetName(), r.GetValueAsUnsigned()))
            open("/tmp/lldb-h2-foreign.txt", "w").write(
                "FOREIGN cur=0x%x id=%d rsp=0x%x kst=0x%x top=0x%x ctx=0x%x tick=%d" %
                (cur, tid, rsp, kst, top, ctx, rd(debugger, ADDR_TICKS)))
            process.Detach()
            return 0
        if hits % 500 == 0:
            print("  hit %d (tick=%d) rsp=0x%x kst=0x%x" % (
                hits, rd(debugger, ADDR_TICKS), rsp, kst))
            sys.stdout.flush()


if __name__ == "__main__":
    sys.exit(main())
