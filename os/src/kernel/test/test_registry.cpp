#include <test.hpp>
#include <logger.hpp>
#include <kernel/test/test_selftest.hpp>

using namespace kernel;

void register_lib_tests();
void register_memory_tests();
void register_ipc_tests();
void register_scheduler_tests();
void register_task_tests();
void register_driver_tests();
void register_vfs_tests();
void register_tmpfs_tests();
void register_tmpfs_invalid_mount_tests();
void register_tmpfs_io_timeout_tests();
void register_tmpfs_corrupted_metadata_tests();
void register_tmpfs_mount_unmount_failure_tests();
void register_signals_tests();
void register_process_tests();
void register_elf_tests();
void register_checked_ptr_tests();
void register_fstab_tests();
void register_rtc_tests();
void register_rlimit_tests();
void register_init_tests();
void register_syscall_tests();
void register_sync_tests();
void register_capability_tests();
void register_task_lifecycle_tests();
void register_idle_task_tests();
void register_vfsd_tests();
void register_iocd_tests();
void register_wfg_tests();
void register_deadlock_detect_tests();
void register_deadlock_recovery_tests();
void register_health_tests();
void register_timer_tests();
void register_serial_tests();
void register_keyboard_tests();
void register_gdt_tests();
void register_idt_tests();
void register_bootparams_tests();
void register_multiboot_tests();
void register_address_tests();
void register_pipe_tests();
void register_vfs_internal_tests();
void register_gcov_tests();
void register_debug_tests();
void register_framebuffer_tests();
void register_stress_tests();
void register_pic_tests();
void register_integration_tests();
void register_pml4_clone_tests();
void register_waitpid_tests();
void register_buffer_pool_tests();
void register_block_device_tests();
void register_fat32_tests();
void register_vfs_fat32_tests();
void register_ipc_blocking_tests();
void register_vfsd_authorization_tests();
void register_syscall_tests();
void register_textutils_tests();
void register_shell_interaction_tests();
void register_irq_guard_tests();
void register_shell_redirect_tests();
void register_klog_tests();
void register_hal_tests();
void register_arch_structure_tests();
void register_buildsystem_tests();
void register_secure_exec_tests();
void register_pci_tests();
void register_virtio_tests();
void register_dma_tests();

// Runmode: kernel
// Testidea: Registers all selftest subsystem test suites by delegating to
// each subsystem's register function
// Input: None
// Expect: All subsystem tests registered (lib, memory, ipc, scheduler, task,
// driver, vfs, signals, process, elf, checked_ptr, rtc, syscall, sync,
// capability, task_lifecycle, idle_task, vfsd, iocd)
// Depends: test framework, all kernel subsystems
void register_selftest_tests() {
    Logger::info("Registering selftest suite");

    register_lib_tests();
    register_memory_tests();
    register_ipc_tests();
    register_scheduler_tests();
    register_task_tests();
    register_driver_tests();
    register_vfs_tests();
    register_tmpfs_invalid_mount_tests();
    register_tmpfs_io_timeout_tests();
    register_tmpfs_corrupted_metadata_tests();
    register_tmpfs_mount_unmount_failure_tests();
    register_signals_tests();
    register_process_tests();
    register_elf_tests();
    register_checked_ptr_tests();
    register_fstab_tests();
    register_rtc_tests();
    register_rlimit_tests();
    register_init_tests();
    register_syscall_tests();
    register_sync_tests();
    register_capability_tests();
    register_task_lifecycle_tests();
    register_idle_task_tests();
    register_vfsd_tests();
    register_iocd_tests();
    register_wfg_tests();
    register_deadlock_detect_tests();
    register_deadlock_recovery_tests();
    register_health_tests();
    register_timer_tests();
    register_serial_tests();
    register_keyboard_tests();
    register_gdt_tests();
    register_idt_tests();
    register_bootparams_tests();
    register_multiboot_tests();
    register_address_tests();
    register_pipe_tests();
    register_vfs_internal_tests();
    register_gcov_tests();
    register_debug_tests();
    register_framebuffer_tests();
    register_stress_tests();
    register_pic_tests();
    register_integration_tests();
    register_pml4_clone_tests();
    register_waitpid_tests();
    register_buffer_pool_tests();
    register_block_device_tests();
    register_fat32_tests();
    register_vfs_fat32_tests();
    register_ipc_blocking_tests();
    register_vfsd_authorization_tests();
    register_tmpfs_tests();
    register_shell_interaction_tests();
    register_irq_guard_tests();
    register_shell_redirect_tests();
    register_klog_tests();
    register_hal_tests();
    register_arch_structure_tests();
    register_buildsystem_tests();
    register_secure_exec_tests();
    register_pci_tests();
    register_virtio_tests();
    register_dma_tests();
}
