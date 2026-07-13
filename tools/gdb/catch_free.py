import gdb

STACK_SIZE = 0x10000

class FreeBP(gdb.Breakpoint):
    def __init__(self): super().__init__("pmm.cpp:240", internal=False)
    def stop(self):
        try:
            phys = int(gdb.parse_and_eval("$rdi"))
            # only log frees in the kernel-stack arena region of interest
            if 0x1680000 <= phys < 0x1800000:
                gdb.write("FREE phys=0x%x\n" % phys)
        except: pass
        return False

class PanicBP(gdb.Breakpoint):
    def __init__(self): super().__init__("kernel.cpp:1161", internal=False)
    def stop(self):
        try:
            cur = gdb.parse_and_eval("kernel::Scheduler::current_task()")
            kstack = int(gdb.parse_and_eval("(uint64_t)((kernel::TaskControlBlock*)%s)->kernel_stack" % cur))
            top = int(gdb.parse_and_eval("(uint64_t)((kernel::TaskControlBlock*)%s)->kernel_stack_top" % cur))
            gdb.write(">>> PANIC current_task kstack=[0x%x-0x%x]\n" % (kstack, top))
            # scan for qwords == 0xb
            slots = []
            off = 0
            while kstack + off < top and len(slots) < 12:
                v = int(gdb.parse_and_eval("(uint64_t)*(uint64_t*)0x%x" % (kstack + off)))
                if v == 0xb:
                    slots.append(kstack + off)
                off += 8
            gdb.write(">>> 0xb slots (addr, off_from_top): %s\n" % ", ".join("0x%x(off=%d)" % (s, top - s) for s in slots))
            if slots:
                gdb.execute("x/16gx 0x%x" % (slots[0] - 0x40))
        except Exception as e:
            gdb.write("panic cur err: %s\n" % e)
        return True

FreeBP(); PanicBP()
