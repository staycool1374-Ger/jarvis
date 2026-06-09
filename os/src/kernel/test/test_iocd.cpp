#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/driver/driver.hpp>

using namespace kernel;

enum IocdMsgType {
    IOCD_REGISTER_DEVICE = 200,
    IOCD_IRQ_EVENT,
    IOCD_MMIO_MAP,
    IOCD_MMIO_UNMAP,
    IOCD_KEYBOARD_READ,
    IOCD_SERIAL_READ,
    IOCD_SERIAL_WRITE,
};

struct IocdMsg {
    uint64_t type;
    uint64_t args[5];
};

// Runmode: kernel
// Testidea: STUB - IOCD boots and registers with the kernel
// Input: None (boot sequence)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/driver
JARVIS_TEST(iocd_boots_and_registers) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Keyboard IRQ is converted to an IPC event
// Input: None (stub test)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/driver
JARVIS_TEST(iocd_keyboard_irq_to_event) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Serial IRQ is converted to an IPC event
// Input: None (stub test)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/driver
JARVIS_TEST(iocd_serial_irq_to_event) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - MMIO region mapped via a capability
// Input: None (stub test)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/driver
JARVIS_TEST(iocd_mmio_map_via_capability) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - MMIO region unmapped when task exits
// Input: None (stub test)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/driver
JARVIS_TEST(iocd_mmio_unmap_on_exit) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Unauthorized MMIO map attempt is rejected
// Input: None (stub test)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/driver
JARVIS_TEST(iocd_unauthorized_mmio_rejected) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Multiple device handlers registered with IOCD
// Input: None (stub test)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/driver
JARVIS_TEST(iocd_multiple_device_handlers) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - IRQ affinity routing works correctly
// Input: None (stub test)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/driver
JARVIS_TEST(iocd_irq_affinity) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all IOCD tests with the test framework
// Input: None
// Expect: All IOCD tests registered
// Depends: test framework
void register_iocd_tests() {
    Logger::info("Registering I/O daemon tests");
    JARVIS_REGISTER_TEST(iocd_boots_and_registers);
    JARVIS_REGISTER_TEST(iocd_keyboard_irq_to_event);
    JARVIS_REGISTER_TEST(iocd_serial_irq_to_event);
    JARVIS_REGISTER_TEST(iocd_mmio_map_via_capability);
    JARVIS_REGISTER_TEST(iocd_mmio_unmap_on_exit);
    JARVIS_REGISTER_TEST(iocd_unauthorized_mmio_rejected);
    JARVIS_REGISTER_TEST(iocd_multiple_device_handlers);
    JARVIS_REGISTER_TEST(iocd_irq_affinity);
}
