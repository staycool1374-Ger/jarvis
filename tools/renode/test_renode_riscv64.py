"""Renode CI test: boot Jarvis RTOS on RISC-V 64 and run tests."""
import os


def test_renode_riscv64():
    USER_DIR = os.path.expanduser("~") + "/jarvis"
    REPL_PATH = USER_DIR + "/tools/renode/jarvis-riscv64.repl"
    KERNEL_PATH = USER_DIR + "/build/kernel-debug.elf"
    OPENSBI_PATH = USER_DIR + "/tools/renode/opensbi-riscv64-generic-fw_dynamic.bin"
    DTB_PATH = USER_DIR + "/tools/renode/jarvis-riscv64.dtb"
    UART_LOG = USER_DIR + "/build/renode_uart.log"

    if os.path.exists(UART_LOG):
        os.remove(UART_LOG)

    machine = emulation.Machine("jarvis-riscv64")
    machine.LoadPlatformDescription(REPL_PATH)

    machine.sysbus.LoadBinary(OPENSBI_PATH, 0x80000000)
    machine.sysbus.LoadELF(KERNEL_PATH)
    machine.sysbus.LoadBinary(DTB_PATH, 0x8FE00000)

    # fw_dynamic info structure at 0x8E000000
    addr = 0x8E000000
    machine.sysbus.WriteDoubleWord(addr, 0x4942534F)
    machine.sysbus.WriteDoubleWord(addr + 4, 0x00000000)
    machine.sysbus.WriteDoubleWord(addr + 8, 0x00000001)
    machine.sysbus.WriteDoubleWord(addr + 12, 0x00000000)
    machine.sysbus.WriteDoubleWord(addr + 16, 0x80200000)
    machine.sysbus.WriteDoubleWord(addr + 20, 0x00000000)
    machine.sysbus.WriteDoubleWord(addr + 24, 0x00000001)
    machine.sysbus.WriteDoubleWord(addr + 28, 0x00000000)
    machine.sysbus.WriteDoubleWord(addr + 32, 0x00000000)
    machine.sysbus.WriteDoubleWord(addr + 36, 0x00000000)
    machine.sysbus.WriteDoubleWord(addr + 40, 0x00000000)
    machine.sysbus.WriteDoubleWord(addr + 44, 0x00000000)

    cpu = machine.sysbus.cpu
    cpu.SetRegister(10, 0)
    cpu.SetRegister(11, 0x8FE00000)
    cpu.SetRegister(12, 0x8E000000)
    cpu.PC = 0x80000000

    uart0 = machine.sysbus.uart0
    rec = uart0.CreateReceiver("recorder")
    rec.Log(UART_LOG)

    emulation.RunFor(120000000000)

    with open(UART_LOG) as f:
        content = f.read()

    assert len(content) > 0, "No UART output received — kernel may not have booted"
    assert "TEST SUMMARY" in content, "Test summary not found in UART output"

    # Print last 30 lines for verification
    lines = content.split("\n")
    for line in lines[-30:]:
        print(line)

    # Check for FAILED count
    found_failed = False
    for line in lines:
        if "FAILED:" in line:
            found_failed = True
            parts = line.split()
            if len(parts) >= 2:
                failed_count = parts[-1].strip()
                assert (
                    failed_count == "0"
                ), f"Tests FAILED: {line.strip()}"
            break
    assert found_failed, "FAILED: line not found in test output"

    emulation.Stop()
