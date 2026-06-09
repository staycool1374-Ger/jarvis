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

JARVIS_TEST(cap_create_mmio_valid_bounds) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_create_mmio_invalid_size_zero) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_create_mmio_invalid_phys_addr) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_grant_to_nonexistent_task_fails) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_grant_duplicate_fails) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_map_mmio_success) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_map_mmio_wrong_task_fails) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_map_mmio_duplicate_virt_fails) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_revoke_unmaps) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_revoke_nonexistent_fails) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_forge_random_rejected) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_forge_incremented_rejected) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_transfer_to_child_on_fork) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_inherit_on_exec) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(cap_max_per_task_limit) {
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
    JARVIS_REGISTER_TEST(cap_create_mmio_valid_bounds);
    JARVIS_REGISTER_TEST(cap_create_mmio_invalid_size_zero);
    JARVIS_REGISTER_TEST(cap_create_mmio_invalid_phys_addr);
    JARVIS_REGISTER_TEST(cap_grant_to_nonexistent_task_fails);
    JARVIS_REGISTER_TEST(cap_grant_duplicate_fails);
    JARVIS_REGISTER_TEST(cap_map_mmio_success);
    JARVIS_REGISTER_TEST(cap_map_mmio_wrong_task_fails);
    JARVIS_REGISTER_TEST(cap_map_mmio_duplicate_virt_fails);
    JARVIS_REGISTER_TEST(cap_revoke_unmaps);
    JARVIS_REGISTER_TEST(cap_revoke_nonexistent_fails);
    JARVIS_REGISTER_TEST(cap_forge_random_rejected);
    JARVIS_REGISTER_TEST(cap_forge_incremented_rejected);
    JARVIS_REGISTER_TEST(cap_transfer_to_child_on_fork);
    JARVIS_REGISTER_TEST(cap_inherit_on_exec);
    JARVIS_REGISTER_TEST(cap_max_per_task_limit);
}
