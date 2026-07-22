import gdb

# BUGS.md#020: the wild write plants a task `entry` (kernel .text code address,
# e.g. 0xFFFF80000026B00C) into a freshly-created task's SAVED CONTEXT FRAME
# (RIP slot at context.rsp+128 and RDI slot at context.rsp+40).  When the
# scheduler switches to that task, it resumes at the kernel address with USER
# CS (0x1B) -> #PF -> SIGSEGV.  This script arms WRITE watchpoints on the
# RIP+RDI slots of each new TCB's context frame, AFTER create_user/create have
# legitimately written `entry` there (so only later wild writes trip).  HW
# watchpoint budget ~4 -> watch up to 2 TCBs (RIP+RDI each).

watched = 0
MAX_TCBS = 2


class SlotWatch(gdb.Breakpoint):
    def __init__(self, addr, tcb, slot):
        expr = "*(unsigned long*)0x%x" % addr
        super().__init__(expr, type=gdb.BP_WATCHPOINT, wp_class=gdb.WP_WRITE, internal=False)
        self.tcb = tcb
        self.slot = slot
        self.addr = addr

    def stop(self):
        gdb.write("\n>>> WILD WRITE to context %s slot (0x%x) of tcb 0x%x\n" % (
            self.slot, self.addr, self.tcb))
        try:
            gdb.execute("bt 20")
        except Exception as e:
            gdb.write("bt err: %s\n" % e)
        try:
            gdb.execute("shell echo PANIC_CAPTURED > build/gdb-panic-captured")
        except Exception:
            pass
        return True


class FrameWatch(gdb.Breakpoint):
    def __init__(self, spec, slot_off, name):
        super().__init__(spec, internal=False)
        self.slot_off = slot_off
        self.name = name

    def stop(self):
        global watched
        if watched >= MAX_TCBS * 2:
            return False
        try:
            tcb = int(gdb.parse_and_eval("(uint64_t)tcb"))
            rsp = int(gdb.parse_and_eval("(uint64_t)tcb->context.rsp"))
        except Exception as e:
            gdb.write("FrameWatch err: %s\n" % e)
            return False
        if tcb == 0 or rsp == 0:
            return False
        addr = rsp + self.slot_off
        try:
            SlotWatch(addr, tcb, self.name)
            watched += 1
            gdb.write("  armed WRITE-watch on context %s (0x%x) of tcb 0x%x\n" % (self.name, addr, tcb))
        except Exception as e:
            gdb.write("  arm err: %s\n" % e)
        return False


FrameWatch("src/kernel/task/task.cpp:458", 128, "RIP")
FrameWatch("src/kernel/task/task.cpp:362", 128, "RIP")
FrameWatch("src/kernel/task/task.cpp:458", 40, "RDI")
FrameWatch("src/kernel/task/task.cpp:362", 40, "RDI")

gdb.write("[watch_frame] armed; target will resume.\n")
