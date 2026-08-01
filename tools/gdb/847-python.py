import gdb
import time

# Connect to QEMU
gdb.execute("target remote :1234")

# Set breakpoint
bp = gdb.Breakpoint("restore_pool_snapshot")

# Continue execution — this will return when breakpoint hits or program exits
gdb.execute("continue")

# Breakpoint hit — dump state
try:
    rdi = int(gdb.parse_and_eval("$rdi"))
    print(f"=== restore_pool_snapshot called ===")
    print(f"RDI (src pointer) = 0x{rdi:016x}")
    
    if rdi > 0xFFFF800100000000:
        print("CORRUPTED pointer detected!")
    
    gdb.execute("info registers rdi rip rax rbx rcx rdx rsi r8 r9 r10 r11 r12 r13 r14 r15 rbp rsp")
    gdb.execute("bt 30")
    
    # Dump memory around the pointer if it's invalid
    if rdi > 0xFFFF800000000000:
        gdb.execute(f"x/32gx {rdi}")
    
    gdb.execute("quit")
except Exception as e:
    print(f"Error: {e}")
    gdb.execute("quit")
