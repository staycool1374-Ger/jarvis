"""Renode CI test: boot Jarvis RTOS on ARM Cortex-A53 and run tests."""
import os
import sys


def test_renode_aarch64():
    USER_DIR = os.path.expanduser("~") + "/jarvis"
    REPL_PATH = USER_DIR + "/tools/renode/jarvis-aarch64.repl"
    KERNEL_PATH = USER_DIR + "/build/kernel-debug.elf"
    UART_LOG = USER_DIR + "/build/renode_uart.log"

    if os.path.exists(UART_LOG):
        os.remove(UART_LOG)

    machine = emulation.Machine("jarvis-aarch64")
    machine.LoadPlatformDescription(REPL_PATH)

    machine.sysbus.LoadELF(KERNEL_PATH)
    machine.cpu0.SetAvailableExceptionLevels(True, False)
    machine.cpu0.PC = 0x40080000
    machine.cpu0.CPSR = 0x3C5

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
