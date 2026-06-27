#if !defined(CONFIG_ARCH_X86_64)

#include <logger.hpp>

void register_pci_tests() {
    kernel::Logger::info("Skipped: register_pci_tests (not supported on this arch)");
}

void register_virtio_tests() {
    kernel::Logger::info("Skipped: register_virtio_tests (not supported on this arch)");
}

void register_fpu_tests() {
    kernel::Logger::info("Skipped: register_fpu_tests (not supported on this arch)");
}

void register_fpu_sse_tests() {
    kernel::Logger::info("Skipped: register_fpu_sse_tests (not supported on this arch)");
}

void register_fpu_clone_tests() {
    kernel::Logger::info("Skipped: register_fpu_clone_tests (not supported on this arch)");
}

void register_fpu_multi_tests() {
    kernel::Logger::info("Skipped: register_fpu_multi_tests (not supported on this arch)");
}

void register_fpu_xmm_all_tests() {
    kernel::Logger::info("Skipped: register_fpu_xmm_all_tests (not supported on this arch)");
}

void register_task_tests() {
    kernel::Logger::info("Skipped: register_task_tests (not supported on this arch)");
}

void register_process_tests() {
    kernel::Logger::info("Skipped: register_process_tests (not supported on this arch)");
}

void register_gdt_tests() {
    kernel::Logger::info("Skipped: register_gdt_tests (not supported on this arch)");
}

void register_idt_tests() {
    kernel::Logger::info("Skipped: register_idt_tests (not supported on this arch)");
}

void register_pic_tests() {
    kernel::Logger::info("Skipped: register_pic_tests (not supported on this arch)");
}

void register_rtc_tests() {
    kernel::Logger::info("Skipped: register_rtc_tests (not supported on this arch)");
}

void register_shell_interaction_tests() {
    kernel::Logger::info("Skipped: register_shell_interaction_tests (not supported on this arch)");
}

void register_buildsystem_tests() {
    kernel::Logger::info("Skipped: register_buildsystem_tests (not supported on this arch)");
}

void register_block_device_tests() {
    kernel::Logger::info("Skipped: register_block_device_tests (not supported on this arch)");
}

#endif // !defined(CONFIG_ARCH_X86_64)
