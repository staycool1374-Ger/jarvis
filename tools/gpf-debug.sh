#!/bin/bash
set -m

SCRIPT_DIR="/Users/arnold/jarvis"
export SCRIPT_DIR

rm -f "$SCRIPT_DIR/build/gdb-panic-captured"

echo "=== Starting QEMU with GDB stub (release build) ==="

# Start expect script in background (handles QEMU + serial interaction)
expect << 'EXPECT_SCRIPT' &
set timeout 120
set stty_init "raw -echo"
spawn qemu-system-x86_64 -cdrom $env(SCRIPT_DIR)/release/jarvis-rtos.iso -m 256M \
    -serial mon:stdio -nic none \
    -boot order=d -s -no-reboot \
    -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd

# Wait for shell prompt
expect {
    -re {jarvis\$ } {
        puts "Shell prompt seen"
    }
    -re {PANIC|CPU EXCEPTION} {
        puts "CRASH AT BOOT"
        exit 1
    }
    timeout {
        puts "TIMEOUT at boot"
        exit 1
    }
}

puts "Sending selftest..."
send "selftest\r"

# Wait for crash
expect {
    -re {KERNEL PANIC|PANIC|CPU EXCEPTION|General Protection Fault} {
        puts "CRASH DETECTED"
        sleep 3
        exit 0
    }
    -re {Self-tests complete\.} {
        puts "Tests completed (no crash)"
        exit 0
    }
    -re {Failed: 0} {
        puts "Test results seen"
    }
    timeout {
        puts "TIMEOUT during selftest"
        exit 1
    }
}
# Wait a bit more for any delayed panic
expect {
    -re {KERNEL PANIC|PANIC|CPU EXCEPTION|General Protection Fault} {
        puts "CRASH DETECTED (late)"
        sleep 3
        exit 0
    }
    timeout { exit 0 }
}
EXPECT_SCRIPT
EXPECT_PID=$!

echo "Waiting for QEMU GDB stub (:1234)..."
for i in 1 2 3 4 5 6 7 8; do
    nc -z localhost 1234 2>/dev/null && break
    sleep 1
done

echo "Running GDB batch script..."
gtimeout 120 x86_64-elf-gdb "$SCRIPT_DIR/build/kernel.elf" -batch -x "$SCRIPT_DIR/tools/gdb/gpf-debug.gdb"
GDB_RC=$?

echo "GDB exit code: $GDB_RC"
wait $EXPECT_PID 2>/dev/null || true

# Kill any lingering QEMU
if kill -0 $(jobs -p) 2>/dev/null; then
    pkill -f "qemu-system-x86_64.*jarvis-rtos" 2>/dev/null || true
fi

if [ -f "$SCRIPT_DIR/build/gdb-panic-captured" ]; then
    echo ""
    echo "!!! KERNEL PANIC WAS CAPTURED BY GDB !!!"
    exit 1
else
    echo ""
    echo "No panic captured."
    exit 0
fi
