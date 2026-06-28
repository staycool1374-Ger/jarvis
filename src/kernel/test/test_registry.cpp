/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>

using namespace kernel;

// ---- forward declarations for per-file registration functions ----

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
void register_spinlock_tests();
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
void register_spsc_tests();
void register_preemption_under_syscall_tests();
void register_spinlock_stress_tests();
void register_atomic_context_switch_tests();
void register_bench_syscall_latency_tests();
void register_bench_irq_latency_tests();
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
void register_textutils_tests();
void register_shell_interaction_tests();
void register_irq_guard_tests();
void register_shell_redirect_tests();
void register_klog_tests();
void register_hal_tests();
void register_buildsystem_tests();
void register_secure_exec_tests();
void register_pci_tests();
void register_virtio_tests();
void register_dma_tests();
void register_net_tests();
void register_ipc_benchmark_tests();
void register_ipc_robustness_tests();
void register_syscall_fuzz_tests();
void register_starvation_deadlock_tests();
void register_resource_exhaustion_tests();
void register_microkernel_transition_tests();
void register_random_tests();
void register_random_vfs_tests();
void register_random_syscall_tests();
void register_random_seed_tests();
void register_fpu_tests();
void register_fpu_sse_tests();
void register_fpu_clone_tests();
void register_fpu_multi_tests();
void register_fpu_xmm_all_tests();
void register_random_vfs_write_tests();
void register_ipc_lock_free_tests();
void register_irqguard_audit_tests();
void register_memory_safety_tests();
void register_sporadic_server_tests();
#if defined(CONFIG_ARCH_AARCH64)
void register_aarch64_tests();
#endif
#if defined(CONFIG_ARCH_RISCV64)
void register_riscv64_tests();
#endif

// ---- Test class table ----
// Each class maps to a lambda that calls the relevant register_*_tests()
// functions.  The "safe" class is the curated TF_RELEASE subset for release
// builds and `selftest` with no args.  The "all" class is everything incl.
// benchmarks.

static constexpr kernel::test::TestClass g_test_classes[] = {
    // -- safe: curated subset with TF_RELEASE tests --
    {"safe", []() {
        register_lib_tests();
        register_checked_ptr_tests();
        register_block_device_tests();
        register_fat32_tests();
        register_vfs_fat32_tests();
        register_waitpid_tests();
        register_shell_interaction_tests();
    }},

    // -- all: everything (debug mode) --
    {"all", []() {
        register_lib_tests();
        register_memory_tests();
        register_ipc_tests();
        register_ipc_robustness_tests();
        register_scheduler_tests();
        register_task_tests();
        register_driver_tests();
        register_vfs_tests();
        register_tmpfs_tests();
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
        register_syscall_fuzz_tests();
        register_sync_tests();
        register_spinlock_tests();
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
        register_spsc_tests();
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
        register_preemption_under_syscall_tests();
        register_spinlock_stress_tests();
        register_atomic_context_switch_tests();
        register_bench_syscall_latency_tests();
        register_bench_irq_latency_tests();
        register_stress_tests();
        register_starvation_deadlock_tests();
        register_pic_tests();
        register_integration_tests();
        register_pml4_clone_tests();
        register_waitpid_tests();
        register_buffer_pool_tests();
        register_resource_exhaustion_tests();
        register_block_device_tests();
        register_fat32_tests();
        register_vfs_fat32_tests();
        register_ipc_blocking_tests();
        register_ipc_lock_free_tests();
        register_vfsd_authorization_tests();
        register_textutils_tests();
        register_shell_interaction_tests();
        register_irq_guard_tests();
        register_irqguard_audit_tests();
        register_shell_redirect_tests();
        register_klog_tests();
        register_hal_tests();
        register_buildsystem_tests();
        register_secure_exec_tests();
        register_pci_tests();
        register_virtio_tests();
        register_dma_tests();
        register_net_tests();
        register_ipc_benchmark_tests();
        register_microkernel_transition_tests();
register_random_tests();
        register_random_vfs_tests();
        register_random_syscall_tests();
        register_random_seed_tests();
        // FPU/SSE tests require GCC 13 or older for inline asm register constraints
        // register_fpu_tests();
        // register_fpu_sse_tests();
        // register_fpu_clone_tests();
        // register_fpu_multi_tests();
        // register_fpu_xmm_all_tests();
        register_random_vfs_write_tests();
        register_memory_safety_tests();
        register_sporadic_server_tests();
#if defined(CONFIG_ARCH_AARCH64)
        register_aarch64_tests();
#endif
#if defined(CONFIG_ARCH_RISCV64)
        register_riscv64_tests();
#endif
    }},

    // -- individual classes --
    {"scheduler", []() {
        register_scheduler_tests();
        register_task_tests();
        register_task_lifecycle_tests();
        register_idle_task_tests();
        register_deadlock_detect_tests();
        register_deadlock_recovery_tests();
        register_health_tests();
        register_timer_tests();
        register_wfg_tests();
        register_starvation_deadlock_tests();
        register_sporadic_server_tests();
    }},

    {"memory", []() {
        register_memory_tests();
        register_checked_ptr_tests();
        register_buffer_pool_tests();
        register_resource_exhaustion_tests();
    }},

    {"ipc", []() {
        register_ipc_tests();
        register_ipc_blocking_tests();
        register_ipc_lock_free_tests();
        register_ipc_robustness_tests();
        register_pipe_tests();
    }},

    {"vfs", []() {
        register_vfs_tests();
        register_vfs_internal_tests();
        register_fstab_tests();
        register_sync_tests();
        register_tmpfs_tests();
        register_tmpfs_invalid_mount_tests();
        register_tmpfs_io_timeout_tests();
        register_tmpfs_corrupted_metadata_tests();
        register_tmpfs_mount_unmount_failure_tests();
        register_vfsd_tests();
        register_iocd_tests();
        register_fat32_tests();
        register_vfs_fat32_tests();
        register_block_device_tests();
    }},

    {"process", []() {
        register_process_tests();
        register_elf_tests();
        register_signals_tests();
        register_rlimit_tests();
        register_waitpid_tests();
        register_pml4_clone_tests();
    }},

    {"syscall", []() {
        register_syscall_tests();
        register_syscall_fuzz_tests();
    }},

    {"arch", []() {
        register_gdt_tests();
        register_idt_tests();
        register_bootparams_tests();
        register_multiboot_tests();
        register_address_tests();
        register_pic_tests();
        register_hal_tests();
#if defined(CONFIG_ARCH_AARCH64)
        register_aarch64_tests();
#endif
#if defined(CONFIG_ARCH_RISCV64)
        register_riscv64_tests();
#endif
    }},

#if defined(CONFIG_ARCH_AARCH64)
    {"arm64", []() {
        register_aarch64_tests();
    }},
#endif

#if defined(CONFIG_ARCH_RISCV64)
    {"risc64", []() {
        register_riscv64_tests();
    }},
#endif

    {"device", []() {
        register_serial_tests();
        register_keyboard_tests();
        register_spsc_tests();
        register_irq_guard_tests();
        register_framebuffer_tests();
        register_rtc_tests();
        register_driver_tests();
    }},

    {"shell", []() {
        register_shell_interaction_tests();
        register_shell_redirect_tests();
        register_textutils_tests();
    }},

    {"net", []() {
        register_net_tests();
        register_pci_tests();
        register_virtio_tests();
        register_dma_tests();
    }},

    {"security", []() {
        register_capability_tests();
        register_secure_exec_tests();
        register_vfsd_authorization_tests();
    }},

    {"debug", []() {
        register_debug_tests();
        register_gcov_tests();
        register_klog_tests();
    }},

    {"integration", []() {
        register_integration_tests();
    }},

    {"stress", []() {
        register_stress_tests();
        register_starvation_deadlock_tests();
    }},

    {"init", []() {
        register_init_tests();
    }},

    {"build", []() {
        register_buildsystem_tests();
    }},

    {"bench", []() {
        register_ipc_benchmark_tests();
        register_microkernel_transition_tests();
        register_bench_syscall_latency_tests();
        register_bench_irq_latency_tests();
    }},

    {"sporadic", []() {
        register_sporadic_server_tests();
    }},
};

static constexpr size_t g_test_class_count =
    sizeof(g_test_classes) / sizeof(g_test_classes[0]);

// ---- register_class ----
// Looks up `name` in g_test_classes[], calls its register_fn, and records
// a class section boundary for output grouping.  Returns true on success.
bool kernel::test::register_class(const char* name) {
    for (size_t i = 0; i < g_test_class_count; ++i) {
        if (strcmp(name, g_test_classes[i].name) == 0) {
            size_t before = Registry::count();
            g_test_classes[i].register_all();
            size_t after = Registry::count();
            if (after > before) {
                Registry::record_class_section(name, before, after - before);
            }
            return true;
        }
    }
    Logger::warn("Unknown test class: %s", name);
    return false;
}
