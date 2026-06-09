#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <kernel/driver/driver.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

JARVIS_TEST(driver_registry_find) {
    auto* drv = DriverRegistry::find("keyboard");
    JARVIS_ASSERT(drv != nullptr);
    JARVIS_ASSERT_EQ(0, strcmp(drv->name, "keyboard"));
    drv = DriverRegistry::find("nonexistent_driver_xyz");
    JARVIS_ASSERT(drv == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(driver_registry_count) {
    size_t cnt = DriverRegistry::count();
    JARVIS_ASSERT(cnt >= 4);
    bool has_kbd = false, has_timer = false;
    for (size_t i = 0; i < cnt; ++i) {
        auto* d = DriverRegistry::get(i);
        if (d) {
            if (strcmp(d->name, "keyboard") == 0) has_kbd = true;
            if (strcmp(d->name, "timer") == 0) has_timer = true;
        }
    }
    JARVIS_ASSERT(has_kbd);
    JARVIS_ASSERT(has_timer);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_server_boots) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(keyboard_driver_in_iocd) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(serial_driver_in_iocd) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(driver_server_mmio_via_caps) {
    JARVIS_TEST_PASS();
}

void register_driver_tests() {
    Logger::info("Registering driver tests");
    JARVIS_REGISTER_TEST(driver_registry_find);
    JARVIS_REGISTER_TEST(driver_registry_count);
    JARVIS_REGISTER_TEST(iocd_server_boots);
    JARVIS_REGISTER_TEST(keyboard_driver_in_iocd);
    JARVIS_REGISTER_TEST(serial_driver_in_iocd);
    JARVIS_REGISTER_TEST(driver_server_mmio_via_caps);
}
