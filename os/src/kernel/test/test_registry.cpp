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
void register_signals_tests();
void register_process_tests();
void register_elf_tests();
void register_checked_ptr_tests();
void register_rtc_tests();
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

// Runmode: kernel
// Testidea: Registers all selftest subsystem test suites by delegating to each subsystem's register function
// Input: None
// Expect: All subsystem tests registered (lib, memory, ipc, scheduler, task, driver, vfs, signals, process, elf, checked_ptr, rtc, syscall, sync, capability, task_lifecycle, idle_task, vfsd, iocd)
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
    register_signals_tests();
    register_process_tests();
    register_elf_tests();
    register_checked_ptr_tests();
    register_rtc_tests();
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
}
