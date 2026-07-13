import gdb

STACK_SIZE = 0x10000
stacks = {}  # phys_base -> (tcb_hex, tid, name, site)

def rec(site):
    try:
        tcb = gdb.parse_and_eval("tcb")
        tp = int(tcb)
        phys = int(gdb.parse_and_eval("tcb->stack_phys_"))
        tid = int(gdb.parse_and_eval("tcb->id"))
        name = gdb.parse_and_eval("tcb->name").string()
        stacks[phys] = ("0x%x" % tp, tid, name, site)
        gdb.write("CREATE[%s] stack phys=0x%x id=%d name=%s tcb=0x%x\n" % (site, phys, tid, name, tp))
    except Exception as e:
        gdb.write("rec err @%s: %s\n" % (site, e))

class CreateA(gdb.Breakpoint):
    def __init__(self): super().__init__("task.cpp:215", internal=False)
    def stop(self): rec("A215"); return False

class CreateB(gdb.Breakpoint):
    def __init__(self): super().__init__("task.cpp:326", internal=False)
    def stop(self): rec("B326"); return False

class CreateC(gdb.Breakpoint):
    def __init__(self): super().__init__("task.cpp:523", internal=False)
    def stop(self): rec("C523"); return False

class FreeBP(gdb.Breakpoint):
    def __init__(self): super().__init__("pmm.cpp:240", internal=False)
    def stop(self):
        try:
            phys = int(gdb.parse_and_eval("$rdi"))
            for base, (tp, tid, name, site) in list(stacks.items()):
                if base <= phys < base + STACK_SIZE:
                    gdb.write(">>> UAF FREE: page 0x%x inside stack of task id=%d name=%s (tcb=%s, alloc@%s)\n" % (phys, tid, name, tp, site))
                    try: gdb.execute("bt 5")
                    except: pass
                    break
        except Exception as e:
            gdb.write("free err: %s\n" % e)
        return False

SLOT = 0xFFFF8000016C3598  # deterministic fault slot

class PostCreate(gdb.Breakpoint):
    def __init__(self): super().__init__("task.cpp:226", internal=False)
    def stop(self):
        try:
            phys = int(gdb.parse_and_eval("tcb->stack_phys_"))
            if phys == 0x16b4000:
                gdb.write(">>> CREATE @0x16b4000 (post-reboot?) slot[0x%x]=0x%x tcb=0x%x\n" % (SLOT, int(gdb.parse_and_eval("(uint64_t)*(uint64_t*)0x%x" % SLOT)), int(gdb.parse_and_eval("tcb"))))
        except Exception as e:
            gdb.write("postcreate err: %s\n" % e)
        return False

class PanicBP(gdb.Breakpoint):
    def __init__(self): super().__init__("kernel.cpp:1161", internal=False)
    def stop(self):
        try:
            rip = int(gdb.parse_and_eval("rip"))
            frsp = int(gdb.parse_and_eval("regs->rsp"))
            fcs = int(gdb.parse_and_eval("regs->cs"))
            gdb.write(">>> PANIC faulting rip=0x%x rsp=0x%x cs=0x%x\n" % (rip, frsp, fcs))
            # dump stack around faulting rsp
            gdb.execute("x/32gx 0x%x" % (frsp - 0x40))
            # try to resolve the return address that faulted (the slot at frsp)
            gdb.write(">>> ret-slot[0x%x] = 0x%x\n" % (frsp, int(gdb.parse_and_eval("(uint64_t)*(uint64_t*)0x%x" % frsp))))
        except Exception as e:
            gdb.write("panic dump err: %s\n" % e)
        try: gdb.execute("bt 8")
        except: pass
        return True

CreateA(); CreateB(); CreateC(); FreeBP(); PostCreate(); PanicBP()
