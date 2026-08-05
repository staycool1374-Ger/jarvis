import lldb

# Verified TCB offsets (x86_64 debug): id=0x360 state=0x370 ctx.rsp=0x478
# kstack=0x488 kstack_top=0x490.  Atoms from nm.
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


def rd(tgt, addr):
    err = lldb.SBError()
    mem = tgt.ReadMemory(addr, 8, err)
    if err.Fail() or mem is None:
        return 0
    return int.from_bytes(bytes(mem), "little")


def h2_foreign(frame, bp_loc, dict):
    tgt = frame.GetThread().GetProcess().GetTarget()
    try:
        with open("/tmp/lldb-hits.txt", "a") as f:
            f.write("hit\n")
    except Exception:
        pass
    cur = rd(tgt, CPU_CURRENT)
    if cur == 0:
        return True
    rsp = frame.GetSP()
    kst = rd(tgt, cur + OFF_KST)
    top = rd(tgt, cur + OFF_KST_TOP)
    if kst == 0 or top == 0:
        return True
    if rsp < kst or rsp >= top:
        tid = rd(tgt, cur + OFF_ID)
        state = rd(tgt, cur + OFF_STATE)
        ctx = rd(tgt, cur + OFF_CTX_RSP)
        print("=== H2-FOREIGN at rate_monotonic_schedule ===")
        print("cur=0x%x id=%d state=%d" % (cur, tid, state))
        print("rsp=0x%x kstack=[0x%x-0x%x] ctx.rsp=0x%x" % (rsp, kst, top, ctx))
        print("tick=%d load_rsp=0x%x save=0x%x next_id=%d gen=%d" % (
            rd(tgt, ADDR_TICKS), rd(tgt, ADDR_LOAD_RSP), rd(tgt, ADDR_SAVE_RSP),
            rd(tgt, ADDR_NEXT_ID), rd(tgt, ADDR_GEN)))
        err2 = lldb.SBError()
        mem = tgt.ReadMemory(rsp, 96, err2)
        if not err2.Fail():
            for i in range(0, 96, 8):
                print("  [rsp+%2d] = 0x%x" % (i, int.from_bytes(bytes(mem[i:i+8]), "little")))
        open("/tmp/lldb-h2-foreign.txt", "w").write(
            "FOREIGN cur=0x%x id=%d rsp=0x%x kst=0x%x top=0x%x ctx=0x%x" %
            (cur, tid, rsp, kst, top, ctx))
        return False  # stop
    return True  # continue
