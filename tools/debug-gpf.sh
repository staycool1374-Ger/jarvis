#!/usr/bin/env bash
# Automated GPF debugging: boot + selftest + GDB trace
set -euo pipefail

ISO="$1"
KERNEL_ELF="$2"

rm -f build/gdb-serial.log build/gdb-panic-captured

# Launch QEMU with serial to file + GDB stub
qemu-system-x86_64 \
    -cdrom "$ISO" -m 256M \
    -serial file:build/gdb-serial.log \
    -boot order=d -s -no-reboot -device isa-debug-exit \
    -drive if=pflash,format=raw,readonly=on,file=/usr/local/share/qemu/edk2-x86_64-code.fd 2>/dev/null &

QEMU_PID=$!

# Wait for GDB stub
for i in 1 2 3 4 5 6 7 8 9 10; do
    if nc -z localhost 1234 2>/dev/null; then break; fi
    sleep 1
done

# Launch QEMU interactively with expect for shell commands
# (use a second QEMU instance with -serial mon:stdio)
qemu-system-x86_64 \
    -cdrom "$ISO" -m 256M \
    -serial mon:stdio \
    -boot order=d -no-reboot -device isa-debug-exit \
    -drive if=pflash,format=raw,readonly=on,file=/usr/local/share/qemu/edk2-x86_64-code.fd &
QEMU2_PID=$!

sleep 2

# Run GDB in batch with our debug script
gtimeout 60 x86_64-elf-gdb "$KERNEL_ELF" \
    -batch -x tools/gdb/debug-gpf.gdb 2>&1 || true

# Check result
if [ -f build/gdb-panic-captured ]; then
    echo ""
    echo "!!! GPF CAPTURED - see GDB output above !!!"
    kill $QEMU_PID $QEMU2_PID 2>/dev/null || true
    exit 1
fi

kill $QEMU_PID $QEMU2_PID 2>/dev/null || true
echo "No GPF detected."
exit 0
