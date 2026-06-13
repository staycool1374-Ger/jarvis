#include <kernel/kernel.hpp>
#include <kernel/arch/gdt.hpp>
#include <kernel/arch/idt.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/keyboard.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/driver/driver.hpp>
#include <kernel/bootparams.hpp>
#include <kernel/sync/sync.hpp>
#include <kernel/multiboot2.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/driver/iocd.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/vfs/initrd_fs.hpp>
#include <kernel/vfs/devfs.hpp>
#include <kernel/vfs/procfs.hpp>
#include <initrd/initrd.hpp>
#include <services/terminal/framebuffer.hpp>
#include <services/terminal/terminal.hpp>
#include <services/program.hpp>
#include <services/shell.hpp>
#include <programs/demo/demo.hpp>
#include <logger.hpp>
#include <test.hpp>
#include <kernel/test/test_selftest.hpp>
#include <constants.hpp>
#include <signal.hpp>

#ifdef CONFIG_PROFILING
extern "C" void gcov_flush_to_serial();
#endif

using namespace arch;

static void debug_putchar(char c) {
    if (c == '\n') {
        while ((inb(arch::COM1_LSR) & 0x20) == 0);
        outb(arch::COM1, '\r');
    }
    while ((inb(arch::COM1_LSR) & 0x20) == 0);
    outb(arch::COM1, c);
}

extern "C" void debug_write(const char* s) {
    while (*s) debug_putchar(*s++);
}

extern "C" {
uint64_t volatile* scheduler_save_rsp_to = nullptr;
uint64_t volatile scheduler_load_rsp_from = 0;
uint64_t volatile scheduler_load_cr3_from = 0;
uint64_t volatile scheduler_next_task_id = 0;
}

extern "C" void debug_write_hex(uint64_t v) {
    char hb[17] = "0000000000000000";
    int pos = 16;
    do { hb[--pos] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; } while (v);
    debug_write(hb + pos);
}

extern "C" void debug_task_switch(uint64_t old_id, uint64_t new_id, uint64_t cr3) {
    debug_write("[SWITCH] old="); debug_write_hex(old_id);
    debug_write(" new="); debug_write_hex(new_id);
    debug_write(" cr3="); debug_write_hex(cr3);
    debug_write("\n");
}

extern "C" {
    uint32_t multiboot_magic = 0;
    uint32_t multiboot_info_ptr = 0;
}
extern char kernel_virt_end[];
extern "C" uint8_t _binary_initrd_cpio_start[];
extern "C" uint8_t _binary_initrd_cpio_end[];

static void init_pic() {
    outb(arch::PIC1_CMD, 0x11);
    outb(arch::PIC2_CMD, 0x11);
    outb(arch::PIC1_DATA, 0x20);
    outb(arch::PIC2_DATA, 0x28);
    outb(arch::PIC1_DATA, 0x04);
    outb(arch::PIC2_DATA, 0x02);
    outb(arch::PIC1_DATA, 0x01);
    outb(arch::PIC2_DATA, 0x01);
    outb(arch::PIC1_DATA, 0x00);
    outb(arch::PIC2_DATA, 0x00);
}

extern "C" void higherhalf_entry(uint64_t magic, uint64_t mb_info) {
    multiboot_magic = magic;
    multiboot_info_ptr = mb_info;
    asm volatile("mov %0, %%rsp\n" : : "r"(kernel_stack + 16_KiB));

    kernel::Logger::init();
    kernel::test::Registry::init();
    kernel::Logger::info("Jarvis RTOS booting");
    debug_write("[BOOT] GDT init...\n");
    arch::GDT::init();
    arch::GDT::load();
    arch::GDT::set_tss_rsp0(reinterpret_cast<uint64_t>(kernel_stack + 16_KiB));
    debug_write("[BOOT] GDT OK\n");

    debug_write("[BOOT] IDT init...\n");
    arch::IDT::init();
    arch::IDT::load();
    debug_write("[BOOT] IDT OK\n");

    debug_write("[BOOT] Memory detection...\n");
    uint64_t mem_size = 64_MiB;
    uint64_t tag_addr = mb2_find_tag(6);
    if (tag_addr) {
        auto* mem_tag = reinterpret_cast<MemoryMapTag*>(tag_addr);
        uint64_t entries = (mem_tag->size - sizeof(MemoryMapTag)) / mem_tag->entry_size;
        for (uint64_t i = 0; i < entries; ++i) {
            auto& entry = mem_tag->entries[i];
            if (entry.type == 1) {
                uint64_t end = entry.base_addr + entry.length;
                if (end > mem_size) mem_size = end;
            }
        }
    }

    uint64_t kend = reinterpret_cast<uint64_t>(kernel_virt_end) - arch::HHDM_OFFSET;
    kernel::PMM::init(mem_size, arch::PAGE_SIZE_2M, kend);
    debug_write("[BOOT] PMM OK\n");

    debug_write("[BOOT] VMM init...\n");
    kernel::VMM::init();
    debug_write("[BOOT] VMM OK\n");

    debug_write("[BOOT] Boot params...\n");
    kernel::BootParams::parse_multiboot_cmdline();
    {
        auto& bp = kernel::BootParams::instance();
        debug_write("[BOOT] timer_hz=");
        debug_write_hex(bp.timer_hz);
        debug_write(" preempt=");
        debug_write(bp.preempt_enabled ? "yes" : "no");
        debug_write(" max_tasks=");
        debug_write_hex(bp.max_tasks);
        debug_write("\n");
    }
    debug_write("[BOOT] Boot params OK\n");

    debug_write("[BOOT] MemPool init...\n");
    kernel::MemPool::init();
    debug_write("[BOOT] MemPool OK\n");

    debug_write("[BOOT] Initrd init...\n");
    initrd::init(_binary_initrd_cpio_start, _binary_initrd_cpio_end);
    debug_write("[BOOT] Initrd OK\n");

    debug_write("[BOOT] VFS init...\n");
    kernel::vfs::init();
    kernel::vfs::mount(kernel::vfs::initrd_fs, "/");
    debug_write("[BOOT] VFS OK (initrd mounted at /)\n");

    debug_write("[BOOT] IPC init...\n");
    kernel::IPC::init();
    debug_write("[BOOT] IPC OK\n");

    debug_write("[BOOT] Syscall init...\n");
    kernel::Syscall::init();
    debug_write("[BOOT] Syscall OK\n");

    debug_write("[BOOT] Scheduler init...\n");
    kernel::Scheduler::init();
    kernel::PMM::set_oom_handler([]() -> bool {
        kernel::TaskControlBlock* victim = nullptr;
        uint64_t victim_priority = ~0ULL;
        for (uint64_t i = 0; i < kernel::Scheduler::task_count(); ++i) {
            auto* t = kernel::Scheduler::task_at(i);
            if (!t || t == kernel::Scheduler::task_at(0)) continue;
            if (t->state != kernel::TaskState::READY
             && t->state != kernel::TaskState::RUNNING) continue;
            if (!t->page_table_) continue;
            if (t->priority < victim_priority) {
                victim = t;
                victim_priority = t->priority;
            }
        }
        if (!victim) return false;
        debug_write("[OOM] Killing task ");
        debug_write_hex(victim->id);
        debug_write(" priority=");
        debug_write_hex(victim->priority);
        debug_write("\n");
        victim->state = kernel::TaskState::TERMINATED;
        victim->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(9));
        kernel::Scheduler::reap_orphans();
        return true;
    });
    debug_write("[BOOT] Scheduler OK\n");

    debug_write("[BOOT] Sync primitives init...\n");
    kernel::sync::init_all();
    debug_write("[BOOT] Sync primitives OK\n");

    debug_write("[BOOT] DriverRegistry init...\n");
    kernel::DriverRegistry::init();
    debug_write("[BOOT] DriverRegistry OK\n");

    debug_write("[BOOT] Framebuffer init...\n");
    service::Framebuffer::init();
    debug_write("[BOOT] Framebuffer OK\n");

    debug_write("[BOOT] Terminal init...\n");
    service::Terminal::init();
    debug_write("[BOOT] Terminal OK\n");

    debug_write("[BOOT] PIC init...\n");
    init_pic();
    debug_write("[BOOT] PIC OK\n");

    debug_write("[BOOT] Keyboard init...\n");
    arch::Keyboard::init();
    arch::IDT::register_handler(
        arch::InterruptVector::KEYBOARD,
        [](uint64_t, uint64_t, uint64_t) {
            arch::Keyboard::handle_irq();
            outb(arch::PIC1_CMD, 0x20);
        }
    );
    debug_write("[BOOT] Keyboard OK\n");

    debug_write("[BOOT] Devfs init...\n");
    kernel::vfs::devfs_init();
    debug_write("[BOOT] Mount devfs...\n");
    kernel::vfs::mount(kernel::vfs::dev_fs, "/dev");
    debug_write("[BOOT] devfs mounted at /dev\n");

    debug_write("[BOOT] Mount procfs...\n");
    kernel::vfs::mount(kernel::vfs::proc_fs, "/proc");
    debug_write("[BOOT] procfs mounted at /proc\n");

    debug_write("[BOOT] Timer init...\n");
    arch::Timer::init(kernel::BootParams::instance().timer_hz);
    debug_write("[BOOT] Timer OK\n");

    kernel::DriverRegistry::register_driver(
        "keyboard", "PS/2 Tastaturtreiber", nullptr, nullptr, 1);
    kernel::DriverRegistry::register_driver(
        "timer", "PIT Timer (1000 Hz)", nullptr, nullptr, 0);
    kernel::DriverRegistry::register_driver(
        "framebuffer", "Framebuffer Grafiktreiber", nullptr, nullptr, 0);
    kernel::DriverRegistry::register_driver(
        "pcspkr", "PC Speaker Soundtreiber", nullptr, nullptr, 0);

    service::ProgramRegistry::init();
    service::ProgramRegistry::register_program(
        "demo", "Mandelbrot set + spinning rectangles (framebuffer)", programs::demo_main);

    kernel::BufferPool::init();
    kernel::daemon::init();

    // Load vfsd userspace daemon before test suite so tests can interact with it
    {
        initrd::InitrdFile f = initrd::find("./vfsd.c.elf");
        if (!f.data) f = initrd::find("vfsd.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
            if (kernel::elf::validate_header(hdr)) {
                auto* vfsd_task = kernel::elf::load(hdr, f.data);
                if (vfsd_task) {
                    vfsd_task->priority = 1;
                    vfsd_task->period_ticks = 10;
                    kernel::Scheduler::add_task(*vfsd_task);
                    kernel::vfsd::set_vfsd_pid(vfsd_task->id);
                    kernel::daemon::register_daemon(
                        "vfsd", "vfsd.c.elf",
                        kernel::vfsd::set_vfsd_pid,
                        kernel::vfsd::get_vfsd_pid);
                    debug_write("[BOOT] VFS daemon started (PID=");
                    debug_write_hex(vfsd_task->id);
                    debug_write(")\n");
                }
            }
        }
    }

    // Load iocd userspace daemon before test suite
    {
        initrd::InitrdFile f = initrd::find("./iocd.c.elf");
        if (!f.data) f = initrd::find("iocd.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
            if (kernel::elf::validate_header(hdr)) {
                auto* iocd_task = kernel::elf::load(hdr, f.data);
                if (iocd_task) {
                    iocd_task->priority = 1;
                    iocd_task->period_ticks = 10;
                    kernel::Scheduler::add_task(*iocd_task);
                    kernel::iocd::set_iocd_pid(iocd_task->id);
                    kernel::daemon::register_daemon(
                        "iocd", "iocd.c.elf",
                        kernel::iocd::set_iocd_pid,
                        kernel::iocd::get_iocd_pid);
                    debug_write("[BOOT] IOCD started (PID=");
                    debug_write_hex(iocd_task->id);
                    debug_write(")\n");
                }
            }
        }
    }

    register_selftest_tests();
    register_ipc_benchmark_tests();

#ifdef CONFIG_DEBUG
    kernel::test::run_debug();
    kernel::Scheduler::cleanup_test_tasks();
    kernel::daemon::reset_daemons();
#else
    kernel::test::run_release();
#endif
    debug_write("[BOOT] Tests done.\n");

    debug_write("[BOOT] Starting userspace test_fork...\n");
    {
        initrd::InitrdFile f = initrd::find("./test_fork.c.elf");
        if (!f.data) f = initrd::find("test_fork.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
            if (kernel::elf::validate_header(hdr)) {
                auto* test_task = kernel::elf::load(hdr, f.data);
                if (test_task) {
                    test_task->priority = 1;
                    test_task->period_ticks = 50;
                    kernel::Scheduler::add_task(*test_task);
                    debug_write("[BOOT] test_fork started (PID=");
                    debug_write_hex(test_task->id);
                    debug_write(")\n");
                }
            }
        }
    }

    // Unprivileged userspace shell (ring 3) via initrd ELF
    {
        initrd::InitrdFile f = initrd::find("./sh.c.elf");
        if (!f.data) f = initrd::find("sh.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
            if (kernel::elf::validate_header(hdr)) {
                auto* shell_task = kernel::elf::load(hdr, f.data);
                if (shell_task) {
                    shell_task->priority = 2;
                    shell_task->period_ticks = 10;
                    kernel::Scheduler::add_task(*shell_task);
                    debug_write("[BOOT] Userspace shell started (PID=");
                    debug_write_hex(shell_task->id);
                    debug_write(")\n");
                }
            }
        }
    }

    // Userspace idle task (Ring 3, pause loop, lowest priority)
    {
        initrd::InitrdFile f = initrd::find("./idle.c.elf");
        if (!f.data) f = initrd::find("idle.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
            if (kernel::elf::validate_header(hdr)) {
                auto* idle_task = kernel::elf::load(hdr, f.data);
                if (idle_task) {
                    idle_task->priority = 0;
                    idle_task->period_ticks = 0xFFFFFFFF;
                    kernel::Scheduler::add_task(*idle_task);
                    debug_write("[BOOT] Userspace idle task started (PID=");
                    debug_write_hex(idle_task->id);
                    debug_write(")\n");
                }
            }
        }
    }

    // Keep kernel shell registered as a fallback (for `runelf` or debug)
    service::Shell::init();

#ifdef CONFIG_PROFILING
    gcov_flush_to_serial();
    arch::qemu_debug_exit(0);
#endif

    debug_write("[BOOT] Boot complete!\n");
    debug_write("[BOOT] Enabling interrupts...\n");
    sti();

    debug_write("[BOOT] Entering idle loop.\n");
    for (uint64_t _i = 0; _i < UINT64_MAX; ++_i) {
        arch::hlt();
    }
    __builtin_unreachable();
}

extern "C" void panic(const char* msg) {
    cli();
    kernel::Logger::fatal("KERNEL PANIC: %s", msg);
    if (service::Terminal::instance()) {
        service::Terminal::set_fg(0xFF0000);
        service::Terminal::write("\nKERNEL PANIC: ");
        service::Terminal::write(msg);
        service::Terminal::set_fg(0xC0C0C0);
    }
    for (uint64_t _i = 0; _i < UINT64_MAX; ++_i) {
        arch::hlt();
    }
    __builtin_unreachable();
}

static void dump_regs(uint64_t* regs) {
    if (!regs) return;

    using L = kernel::Logger;

    L::raw_write("  RAX: "); L::print_hex(regs[0]);
    L::raw_write("  RBX: "); L::print_hex(regs[1]); L::raw_write("\n");
    L::raw_write("  RCX: "); L::print_hex(regs[2]);
    L::raw_write("  RDI: "); L::print_hex(regs[5]); L::raw_write("\n");
    L::raw_write("  RDX: "); L::print_hex(regs[3]);
    L::raw_write("  RSI: "); L::print_hex(regs[4]); L::raw_write("\n");
    L::raw_write("  RBP: "); L::print_hex(regs[6]);
    L::raw_write("  RSP: "); L::print_hex(reinterpret_cast<uint64_t>(&regs)); L::raw_write("\n");
    L::raw_write("  R8:  "); L::print_hex(regs[7]);
    L::raw_write("  R9:  "); L::print_hex(regs[8]); L::raw_write("\n");
    L::raw_write("  R10: "); L::print_hex(regs[9]);
    L::raw_write("  R11: "); L::print_hex(regs[10]); L::raw_write("\n");
    L::raw_write("  R12: "); L::print_hex(regs[11]);
    L::raw_write("  R13: "); L::print_hex(regs[12]); L::raw_write("\n");
    L::raw_write("  R14: "); L::print_hex(regs[13]);
    L::raw_write("  R15: "); L::print_hex(regs[14]); L::raw_write("\n");
    L::raw_write("  RIP: "); L::print_hex(regs[17]);
    L::raw_write("  CS:  "); L::print_hex(regs[18]); L::raw_write("\n");
    L::raw_write("  RFL: "); L::print_hex(regs[19]); L::raw_write("\n");

    uint64_t cr0 = read_cr0();
    uint64_t cr2 = read_cr2();
    uint64_t cr3 = read_cr3();
    uint64_t cr4 = read_cr4();

    L::raw_write("  CR0: "); L::print_hex(cr0); L::raw_write("\n");
    L::raw_write("  CR2: "); L::print_hex(cr2);
    L::raw_write("  CR3: "); L::print_hex(cr3); L::raw_write("\n");
    L::raw_write("  CR4: "); L::print_hex(cr4); L::raw_write("\n");

    L::raw_write("  Stack trace:\n");
    uint64_t* rbp = reinterpret_cast<uint64_t*>(regs[6]);
    for (int i = 0; i < 8 && rbp && rbp[1]; ++i) {
        L::raw_write("    ["); L::print_dec(i); L::raw_write("] ");
        L::print_hex(rbp[1]);
        L::raw_write("\n");
        rbp = reinterpret_cast<uint64_t*>(rbp[0]);
    }
}

static const char* exception_name(uint64_t vector) {
    switch (vector) {
    case 0:  return "Division by Zero";
    case 1:  return "Debug";
    case 2:  return "NMI";
    case 3:  return "Breakpoint";
    case 4:  return "Overflow";
    case 5:  return "Bound Range";
    case 6:  return "Invalid Opcode";
    case 7:  return "Device Not Available";
    case 8:  return "Double Fault";
    case 10: return "Invalid TSS";
    case 11: return "Segment Not Present";
    case 12: return "Stack Segment Fault";
    case 13: return "General Protection Fault";
    case 14: return "Page Fault";
    case 16: return "x87 FPU Error";
    case 17: return "Alignment Check";
    case 18: return "Machine Check";
    case 19: return "SIMD FP Exception";
    case 30: return "Security Exception";
    default: return "Reserved";
    }
}

extern "C" uint64_t syscall_handler(uint64_t number, uint64_t arg0, uint64_t arg1,
                                    uint64_t arg2, uint64_t arg3, uint64_t* regs);

/// @brief Delivers a signal to a user task by setting up a signal frame on the user stack.
///        Modifies regs to return to the user's registered signal handler (or terminates
///        the task if no handler is registered and the default action is to terminate).
/// @return true if signal was delivered (handler will run), false if task was terminated.
static bool deliver_signal_to_user(kernel::TaskControlBlock* t, uint64_t sig,
                                   uint64_t vector, uint64_t error_code,
                                   uint64_t rip, uint64_t* regs)
{
    if (!t || !regs) return false;

    // SIGKILL is always fatal — cannot be caught or ignored
    if (kernel::signal_is_fatal(sig)) {
        kernel::Logger::error("Task %x: SIGKILL (fatal, no handler allowed)", t->id);
        t->state = kernel::TaskState::TERMINATED;
        t->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig));
        return false;
    }

    // If the task has a registered handler, invoke it
    if (t->has_signal_handler(sig)) {
        kernel::Logger::info("Task %x: delivering signal %x to handler at %x",
            t->id, sig, reinterpret_cast<uint64_t>(t->get_signal_handler(sig)));

        uint64_t user_rsp = regs[20];  // current user RSP
        // Align stack: push SignalFrame
        user_rsp -= sizeof(kernel::SignalFrame);
        // Align to 16 bytes: (RSP + 8) % 16 == 0 at handler entry
        // After pushing SignalFrame, we adjust so that at handler entry:
        // RSP is 8 mod 16 (because call pushes return addr)
        user_rsp &= ~0xFULL;

        auto* frame = reinterpret_cast<kernel::SignalFrame*>(
            arch::HHDM_OFFSET + kernel::VMM::virt_to_phys_in_pml4(user_rsp, t->page_table_));
        if (!frame) {
            // If we cannot write the signal frame (bad stack), terminate
            kernel::Logger::error("Task %x: cannot write signal frame, terminating", t->id);
            t->state = kernel::TaskState::TERMINATED;
            t->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig));
            return false;
        }

        *frame = kernel::SignalFrame{
            .sig = sig,
            .saved_rip = regs[17],
            .saved_rsp = regs[20],
            .saved_rflags = regs[19],
            .saved_cs = regs[18],
            .saved_ss = regs[21],
            .reserved = {0, 0}
        };

        // Modify return context to go to the signal handler
        regs[5] = sig;                   // RDI = signal number (first argument)
        regs[17] = reinterpret_cast<uint64_t>(t->get_signal_handler(sig));  // RIP = handler
        regs[20] = user_rsp;             // RSP = adjusted user stack
        // Clear direction flag and RF
        regs[19] = arch::RFLAGS_DEFAULT; // RFLAGS with IF set

        return true;
    }

    // No handler registered — check default action
    auto action = kernel::default_signal_action(sig);
    if (action == kernel::SignalAction::IGNORE) {
        kernel::Logger::warn("Task %x: signal %x ignored (default)", t->id, sig);
        return true;  // ignored, resume execution
    }

    // Default is to terminate
    kernel::Logger::error("Task %x: unhandled signal %x vector=%x rip=%x err=%x",
        t->id, sig, vector, rip, error_code);
    if (vector == 14) {
        uint64_t cr2_val = read_cr2();
        kernel::Logger::error("  CR2=%x", cr2_val);
    }
    dump_regs(regs);
    t->state = kernel::TaskState::TERMINATED;
    t->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig));
    return false;
}

extern "C" void handle_interrupt_c(uint64_t vector, uint64_t error_code, uint64_t rip,
                                   uint64_t* regs)
{
    if (vector < 32) {
        auto* t = kernel::Scheduler::current_task();
        uint64_t cs = regs ? regs[18] : 0;
        bool from_user = (cs == arch::SEG_USER_CODE || cs == arch::SEG_USER_DATA);

        if (from_user && t) {
            // Check fault recovery first: if we're inside a safe_copy_from_user zone,
            // redirect to the recovery IP instead of delivering a signal.
            if (kernel::g_user_access_recover_ip) {
                regs[17] = kernel::g_user_access_recover_ip;
                kernel::g_user_access_recover_ip = 0;
                return;
            }

            auto mapping = kernel::exception_to_signal(vector);
            uint64_t sig = static_cast<uint64_t>(mapping.signal);
            kernel::Logger::warn("Task %x: exception vector=%x (%s) → signal %x",
                t->id, vector, mapping.name, sig);

            if (vector == 14) {
                kernel::Logger::error("  CR2=%x", read_cr2());
            }

            bool was_delivered = deliver_signal_to_user(t, sig, vector, error_code, rip, regs);
            if (!was_delivered && t->state == kernel::TaskState::TERMINATED) {
                // If task was terminated, reschedule on return
                kernel::Scheduler::reschedule();
            }
            return;
        }

        kernel::Logger::fatal("CPU EXCEPTION: %s (vector=%x err=%x rip=%x)",
            exception_name(vector), vector, error_code, rip);
        dump_regs(regs);

        if (t) {
            kernel::Logger::raw_write("  Task: id=");
            kernel::Logger::print_hex(t->id);
            kernel::Logger::raw_write(" prio=");
            kernel::Logger::print_dec(t->priority);
            kernel::Logger::raw_write(" state=");
            kernel::Logger::print_hex(static_cast<uint64_t>(t->state));
            kernel::Logger::raw_write("\n");
        }

        panic("CPU EXCEPTION");
        return;
    }

    if (vector == 0x80) {
        regs[0] = syscall_handler(regs[0], regs[1], regs[2], regs[3], regs[4], regs);
        auto* task = kernel::Scheduler::current_task();

        // After syscall, check for pending signals on the current task
        if (task && task->pending_signals && task->page_table_ != 0) {
            // Find the highest-priority pending signal
            uint64_t sig = __builtin_ctzll(task->pending_signals);
            if (sig < 32) {
                kernel::Logger::debug("Task %x: pending signal %x after syscall", task->id, sig);
                deliver_signal_to_user(task, sig, 0, 0, regs[17], regs);
                task->pending_signals &= ~(1ULL << sig);
            }
        }

        if (task && (task->state == kernel::TaskState::TERMINATED ||
                     task->state == kernel::TaskState::BLOCKED)) {
            kernel::Scheduler::reschedule();
        }
        return;
    }

    arch::IDT::handle_interrupt(vector, error_code, rip);

    if (vector >= 32 && vector < 48) {
        outb(arch::PIC1_CMD, 0x20);
        if (vector >= 40) outb(arch::PIC2_CMD, 0x20);
    }
}

extern "C" uint64_t syscall_handler(uint64_t number, uint64_t arg0, uint64_t arg1,
                                    uint64_t arg2, uint64_t arg3, uint64_t* regs)
{
    return kernel::Syscall::handle(number, arg0, arg1, arg2, arg3, regs);
}

uint8_t kernel_stack[16_KiB] __attribute__((section(".bss")));
