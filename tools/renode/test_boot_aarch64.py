"""Tests that the aarch64 kernel boots and produces UART output on Renode."""
import os

USER_DIR = os.path.expanduser("~") + "/jarvis"
REPL_PATH = USER_DIR + "/tools/renode/jarvis-aarch64.repl"
KERNEL_PATH = USER_DIR + "/build/kernel-debug.elf"
UART_LOG = USER_DIR + "/build/renode_uart.log"

def test_boot():
    machine = emulation.Machine("jarvis-aarch64")
    machine.LoadPlatformDescription(REPL_PATH)
    machine.sysbus.LoadELF(KERNEL_PATH)
    machine.cpu0.PC = 0x40080000
    machine.cpu0.CPSR = 0x3C5

    uart0 = machine.sysbus.uart0
    rec = uart0.CreateReceiver("recorder")
    rec.Log(UART_LOG)

    emulation.RunFor(500000000)  # 500ms

    with open(UART_LOG) as f:
        content = f.read()

    assert len(content) > 0, "No UART output received - kernel may not have booted"

    emulation.Stop()
