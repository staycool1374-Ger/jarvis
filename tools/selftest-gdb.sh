#!/usr/bin/env bash
# Run release ISO under GDB, automate selftest via serial pipe
set -euo pipefail

KERNEL_ELF="build/kernel.elf"
ISO="release/jarvis-rtos.iso"

rm -f build/gdb-panic-captured /tmp/qemu-serial.in /tmp/qemu-serial.out

# Create named pipes
mkfifo /tmp/qemu-serial.in /tmp/qemu-serial.out 2>/dev/null || true

# Launch QEMU with serial pipe + GDB stub
qemu-system-x86_64 \
    -cdrom "$ISO" -m 256M \
    -serial pipe:/tmp/qemu-serial \
    -boot order=d -s -no-reboot -device isa-debug-exit \
    -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd 2>/dev/null &

QEMU_PID=$!

# Wait for GDB stub
for i in 1 2 3 4 5 6 7 8 9 10; do
    if nc -z localhost 1234 2>/dev/null; then break; fi
    sleep 1
done
echo "GDB stub ready (PID=$QEMU_PID)."

# Launch GDB in background (monitors for GPF)
gtimeout 120 x86_64-elf-gdb "$KERNEL_ELF" -batch -x tools/gdb/gpf-only.gdb 2>&1 &
GDB_PID=$!

# Let GDB connect and initialize
sleep 3

# Read serial output from the pipe (non-blocking with cat)
# Use a background process to read the output pipe
cat /tmp/qemu-serial.out &
CAT_PID=$!

# Wait for shell prompt on serial
echo "Waiting for shell prompt..."
PATTERN="jarvis\$ "
while true; do
    if read -t 0.5 line < /tmp/qemu-serial.out 2>/dev/null; then
        echo "SERIAL: $line"
        if echo "$line" | grep -q "jarvis\$"; then
            echo "Shell prompt found!"
            break
        fi
        if echo "$line" | grep -qi "panic\|exception"; then
            echo "PANIC detected!"
            kill $QEMU_PID $GDB_PID $CAT_PID 2>/dev/null || true
            exit 1
        fi
    fi
    if ! kill -0 $QEMU_PID 2>/dev/null; then
        echo "QEMU exited!"
        break
    fi
    # Check if GDB caught GPF
    if [ -f build/gdb-panic-captured ]; then
        echo "!!! GPF CAPTURED BY GDB !!!"
        kill $QEMU_PID $CAT_PID 2>/dev/null || true
        exit 1
    fi
done

# Send selftest command
echo "Sending 'selftest'..."
echo "selftest" > /tmp/qemu-serial.in

# Monitor output until tests complete
echo "Waiting for tests..."
while true; do
    if read -t 1 line < /tmp/qemu-serial.out 2>/dev/null; then
        echo "SERIAL: $line"
        if echo "$line" | grep -q "Failed:"; then
            echo "Test results: $line"
            sleep 2
            break
        fi
        if echo "$line" | grep -qi "panic\|exception\|GPF"; then
            echo "PANIC/EXCEPTION detected!"
            kill $QEMU_PID $GDB_PID $CAT_PID 2>/dev/null || true
            exit 1
        fi
    fi
    if [ -f build/gdb-panic-captured ]; then
        echo "!!! GPF CAPTURED BY GDB !!!"
        kill $QEMU_PID $CAT_PID 2>/dev/null || true
        exit 1
    fi
    if ! kill -0 $QEMU_PID 2>/dev/null; then
        echo "QEMU exited!"
        break
    fi
done

# Wait for post-test GPF
echo "Tests complete. Watching for GPF after daemon restart..."
for i in $(seq 1 30); do
    sleep 1
    if [ -f build/gdb-panic-captured ]; then
        echo "!!! GPF CAPTURED BY GDB (after tests) !!!"
        kill $QEMU_PID $CAT_PID 2>/dev/null || true
        exit 1
    fi
    if read -t 0 line < /tmp/qemu-serial.out 2>/dev/null; then
        echo "SERIAL: $line"
        if echo "$line" | grep -qi "panic\|exception"; then
            echo "PANIC on serial!"
            kill $QEMU_PID $GDB_PID $CAT_PID 2>/dev/null || true
            exit 1
        fi
    fi
done

echo "No GPF detected within 30s after tests."
kill $QEMU_PID $GDB_PID $CAT_PID 2>/dev/null || true
exit 0
