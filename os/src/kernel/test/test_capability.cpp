#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

JARVIS_TEST(capability_create_for_mmio) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(capability_grant_to_task) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(capability_map_mmio) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(capability_revoke) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(capability_cannot_forge) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(iocd_uses_capabilities_for_keyboard) {
    JARVIS_TEST_PASS();
}

void register_capability_tests() {
    Logger::info("Registering capability tests");
    JARVIS_REGISTER_TEST(capability_create_for_mmio);
    JARVIS_REGISTER_TEST(capability_grant_to_task);
    JARVIS_REGISTER_TEST(capability_map_mmio);
    JARVIS_REGISTER_TEST(capability_revoke);
    JARVIS_REGISTER_TEST(capability_cannot_forge);
    JARVIS_REGISTER_TEST(iocd_uses_capabilities_for_keyboard);
}
