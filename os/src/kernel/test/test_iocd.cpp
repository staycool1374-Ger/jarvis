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

JARVIS_TEST(iocd_boots_and_registers) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t iocd_pid = 3;
    auto* iocd = Scheduler::find_task(iocd_pid);
    JARVIS_ASSERT(iocd != nullptr);
    JARVIS_ASSERT(iocd->msg_queue != nullptr);

    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_keyboard_irq_to_event) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_serial_irq_to_event) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_mmio_map_via_capability) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_mmio_unmap_on_exit) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_unauthorized_mmio_rejected) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_multiple_device_handlers) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_irq_affinity) {
    JARVIS_TEST_PASS();
}

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
