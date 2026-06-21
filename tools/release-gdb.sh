#!/usr/bin/env bash
# Run release ISO under GDB to catch GPF after daemon restart
set -euo pipefail

rm -f build/gdb-panic-captured build/gdb-serial.log

KERNEL_ELF="build/kernel.elf"
ISO="release/jarvis-rtos.iso"

# Launch QEMU with serial to file + GDB stub
qemu-system-x86_64 \
    -cdrom "$ISO" -m 256M \
    -serial file:build/gdb-serial.log \
    -boot order=d -s -no-reboot -device isa-debug-exit \
    -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd 2>/dev/null &
QEMU_PID=$!

# Wait for GDB stub
for i in 1 2 3 4 5 6 7 8 9 10; do
    if nc -z localhost 1234 2>/dev/null; then
        echo "GDB stub ready on :1234"
        break
    fi
    sleep 1
done

# Connect GDB with our debug script
echo "Connecting GDB..."
gtimeout 120 x86_64-elf-gdb "$KERNEL_ELF" \
    -batch -x tools/gdb/debug-gpf.gdb 2>&1 || true

# Check result
if [ -f build/gdb-panic-captured ]; then
    echo ""
    echo "!!! GPF CAPTURED !!!"
    rm -f build/gdb-panic-captured
    kill $QEMU_PID 2>/dev/null || true
    exit 1
fi

echo "No GPF detected (clean boot)."
kill $QEMU_PID 2>/dev/null || true
exit 0
