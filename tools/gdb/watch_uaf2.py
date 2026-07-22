import gdb

TCB_MAGIC = 0x5443424D41474943

# Since the QEMU stub auto-resumes after `target remote`, we do NOT issue a
# top-level `continue` from a sourced command file (it errors "target running");
# the Python breakpoint setup runs during sourcing while stopped, then the
# target resumes on its own.

class CleanupBP(gdb.Breakpoint):
    def __init__(self):
        # Break at the very start of cleanup(), BEFORE it clears magic (line
        # 854).  At entry `this->magic == TCB_MAGIC` for a live TCB.  Arming an
        # access watchpoint here catches the instant this block is later touched
        # after it is freed (the use-after-free) — whether a stale scheduler
        # read of the 0xDD-poisoned block, or create()'s memset() reuse.
        super().__init__("TaskControlBlock::cleanup()", internal=False)
    def stop(self):
        try:
            this = int(gdb.parse_and_eval("this"))
        except Exception as e:
            gdb.write("cleanup this err: %s\n" % e)
            return False
        if this == 0:
            return False
        try:
            magic = int(gdb.parse_and_eval("(uint64_t)*(uint64_t*)0x%x" % this))
        except Exception:
            return False
        if magic != TCB_MAGIC:
            return False
        try:
            tid = int(gdb.parse_and_eval("((kernel::TaskControlBlock*)0x%x)->id" % this))
        except Exception:
            tid = -1
        gdb.write(">>> TCB cleanup @ 0x%x id=%d (arming awatch)\n" % (this, tid))
        try:
            TcbWatch(this)
        except Exception as e:
            gdb.write("  awatch err: %s\n" % e)
        return False


class TcbWatch(gdb.Breakpoint):
    def __init__(self, addr):
        # WP_ACCESS = read OR write
        super().__init__("(uint64_t*)0x%x" % addr, type=gdb.BP_WATCHPOINT,
                         wp_class=gdb.WP_ACCESS, internal=False)
        self.addr = addr
        gdb.write("  armed awatch on freed TCB 0x%x\n" % addr)
    def stop(self):
        gdb.write("\n>>> UAF ACCESS on freed TCB 0x%x <--\n" % self.addr)
        try:
            gdb.execute("bt 16")
        except Exception as e:
            gdb.write("bt err: %s\n" % e)
        try:
            gdb.execute("shell echo PANIC_CAPTURED > build/gdb-panic-captured")
        except Exception:
            pass
        return True  # stop and let user/timeout inspect


class PanicBP(gdb.Breakpoint):
    def __init__(self):
        super().__init__("panic", internal=False)
    def stop(self):
        try:
            msg = str(gdb.parse_and_eval("(const char*)$arg0"))
        except Exception:
            msg = "?"
        gdb.write("\n>>> PANIC: %s\n" % msg)
        try:
            gdb.execute("bt 16")
        except Exception:
            pass
        try:
            gdb.execute("shell echo PANIC_CAPTURED > build/gdb-panic-captured")
        except Exception:
            pass
        return True


CleanupBP()
PanicBP()
gdb.write("[watch_uaf2] breakpoints armed; target will resume.\n")
