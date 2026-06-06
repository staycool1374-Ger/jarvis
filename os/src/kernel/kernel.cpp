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
#include <kernel/syscall/syscall.hpp>
#include <kernel/driver/driver.hpp>
#include <kernel/test/test.hpp>
#include <kernel/bootparams.hpp>
#include <kernel/sync/sync.hpp>
#include <kernel/multiboot2.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/initrd_fs.hpp>
#include <kernel/vfs/devfs.hpp>
#include <kernel/vfs/procfs.hpp>
#include <initrd/initrd.hpp>
#include <services/terminal/framebuffer.hpp>
#include <services/terminal/terminal.hpp>
#include <services/shell.hpp>
#include <services/program.hpp>
#include <programs/demo/demo.hpp>

static constexpr uint32_t DEFAULT_TIMER_HZ = 1000;

/// @brief Wenn 1, werden alle Selbsttests beim Boot ausgeführt und via Serial ausgegeben.
static constexpr bool RUN_SELFTEST_ON_BOOT = true;

using namespace arch;

static void debug_putchar(char c) {
    if (c == '\n') {
        while ((inb(0x3F8 + 5) & 0x20) == 0);
        outb(0x3F8, '\r');
    }
    while ((inb(0x3F8 + 5) & 0x20) == 0);
    outb(0x3F8, c);
}

extern "C" void debug_write(const char* s) {
    while (*s) debug_putchar(*s++);
}

static void debug_init_serial() {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x01);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
    outb(0x3F8 + 4, 0x0F);
    debug_write("[SERIAL] OK\n");
}

extern "C" {
uint64_t volatile* scheduler_save_rsp_to = nullptr;
uint64_t volatile scheduler_load_rsp_from = 0;
uint64_t volatile scheduler_load_cr3_from = 0;
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

volatile bool g_user_task_ran = false;

extern "C" {
    uint32_t multiboot_magic = 0;
    uint32_t multiboot_info_ptr = 0;
}
extern char kernel_virt_end[];
extern "C" uint8_t _binary_initrd_cpio_start[];
extern "C" uint8_t _binary_initrd_cpio_end[];

static void init_pic() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x00);
    outb(0xA1, 0x00);
}

extern "C" void higherhalf_entry(uint64_t magic, uint64_t mb_info) {
    multiboot_magic = magic;
    multiboot_info_ptr = mb_info;
    asm volatile("mov %0, %%rsp\n" : : "r"(kernel_stack + 16_KiB));

    debug_init_serial();
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

    uint64_t kend = reinterpret_cast<uint64_t>(kernel_virt_end) - 0xFFFF800000000000ULL;
    kernel::PMM::init(mem_size, 0x200000, kend);
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
    kernel::vfs::mount(&kernel::vfs::initrd_fs, "/");
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
            if (t->state != kernel::TaskState::READY && t->state != kernel::TaskState::RUNNING) continue;
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
            kernel::vfs::tty_wake_readers();
            outb(0x20, 0x20);
        }
    );
    debug_write("[BOOT] Keyboard OK\n");

    debug_write("[BOOT] TestRegistry init...\n");
    kernel::test::TestRegistry::init();
    debug_write("[BOOT] TestRegistry OK\n");

    debug_write("[BOOT] Devfs init...\n");
    kernel::vfs::devfs_init();
    debug_write("[BOOT] Mount devfs...\n");
    kernel::vfs::mount(&kernel::vfs::dev_fs, "/dev");
    debug_write("[BOOT] devfs mounted at /dev\n");

    debug_write("[BOOT] Mount procfs...\n");
    kernel::vfs::mount(&kernel::vfs::proc_fs, "/proc");
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
        "demo", "Grafik-Demos (Mandelbrot, Rotation)", programs::demo_main);

    if constexpr (RUN_SELFTEST_ON_BOOT) {
        debug_write("[BOOT] Creating selftest runner...\n");
        auto* test_task = kernel::TaskControlBlock::create([]() {
            debug_write("[TEST DEBUG] Test runner started!\n");
            auto report = kernel::test::TestRegistry::run_all();
            if (service::Terminal::instance()) service::Terminal::set_fg(0xC0C0C0);
            auto tprint = [](const char* s) {
                if (service::Terminal::instance()) service::Terminal::write(s);
                else debug_write(s);
            };
            auto tprint_n = [&tprint](uint64_t n) {
                char buf[32];
                int pos = 32;
                buf[--pos] = '\0';
                do { buf[--pos] = '0' + (n % 10); n /= 10; } while (n);
                tprint(buf + pos);
            };
            tprint("[TEST] Total: "); tprint_n(report.total);
            tprint(" Pass: "); tprint_n(report.passed);
            tprint(" Fail: "); tprint_n(report.failed);
            tprint(" Skip: "); tprint_n(report.skipped);
            tprint("\n");

            for (size_t i = 0; i < kernel::test::TestRegistry::count(); ++i) {
                auto* t = kernel::test::TestRegistry::get(i);
                if (!t) continue;
                tprint("[TEST] ");
                if (t->result == kernel::test::TestResult::PASS) tprint("PASS  ");
                else if (t->result == kernel::test::TestResult::FAIL) tprint("FAIL  ");
                else tprint("SKIP  ");
                tprint(t->name);
                if (t->failure_msg) { tprint(": "); tprint(t->failure_msg); }
                tprint("\n");
            }

            kernel::Scheduler::current_task()->state = kernel::TaskState::TERMINATED;
            while (true) asm volatile("hlt");
        }, 2, 10);
        if (!test_task) panic("Cannot create test task");
        kernel::Scheduler::add_task(test_task);
    }

    debug_write("[BOOT] Creating shell task...\n");
    auto* shell_task = kernel::TaskControlBlock::create(
        service::Shell::shell_task_main, 1, 50);
    if (!shell_task) panic("Cannot create shell task");
    kernel::Scheduler::add_task(shell_task);

    debug_write("[BOOT] Boot complete! Enabling interrupts...\n");
    sti();

    debug_write("[BOOT] Entering idle loop.\n");
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void panic(const char* msg) {
    cli();
    debug_write("\n!!! KERNEL PANIC: ");
    debug_write(msg);
    debug_write(" !!!\n");
    if (service::Terminal::instance()) {
        service::Terminal::set_fg(0xFF0000);
        service::Terminal::write("\nKERNEL PANIC: ");
        service::Terminal::write(msg);
        service::Terminal::set_fg(0xC0C0C0);
    }
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" uint64_t syscall_handler(uint64_t number, uint64_t arg0, uint64_t arg1,
                                    uint64_t arg2, uint64_t arg3, uint64_t* regs);

extern "C" void handle_interrupt_c(uint64_t vector, uint64_t error_code, uint64_t rip,
                                   uint64_t* regs)
{
    if (vector < 32) {
        auto* t = kernel::Scheduler::current_task();
        uint64_t cs = regs ? regs[18] : 0;
        bool from_user = (cs == 0x1B || cs == 0x23);

        if (from_user && t) {
            static const char* sig_name[] = {
                "SIGFPE", "SIGSEGV", "SIGSEGV", "SIGSEGV", "SIGILL"
            };
            static const uint64_t sig_num[] = {8, 11, 11, 11, 4};
            uint64_t sig_idx = 0;
            if (vector == 0) sig_idx = 0;
            else if (vector == 6) sig_idx = 4;
            else if (vector == 13 || vector == 14) sig_idx = 1;
            else sig_idx = 1;

            debug_write("\n[EXCEPTION] Task ");
            debug_write_hex(t->id);
            debug_write(" received ");
            debug_write(sig_name[sig_idx]);
            debug_write(" (signal ");
            debug_write_hex(sig_num[sig_idx]);
            debug_write(") vector=");
            debug_write_hex(vector);
            debug_write(" rip=");
            debug_write_hex(rip);
            debug_write(" err=");
            debug_write_hex(error_code);
            if (vector == 14) {
                uint64_t cr2_val;
                asm volatile("mov %%cr2, %0" : "=r"(cr2_val));
                debug_write(" cr2=");
                debug_write_hex(cr2_val);
            }
            debug_write(" — terminating\n");
            t->state = kernel::TaskState::TERMINATED;
            t->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig_num[sig_idx]));
            return;
        }

        debug_write("\n!!! EXCEPTION vector=");
        debug_write_hex(vector);
        debug_write(" err=");
        debug_write_hex(error_code);
        debug_write(" rip=");
        debug_write_hex(rip);
        uint64_t cr2_val;
        asm volatile("mov %%cr2, %0" : "=r"(cr2_val));
        debug_write(" cr2=");
        debug_write_hex(cr2_val);
        debug_write(" task=");
        if (t) {
            debug_write_hex(t->id);
            debug_write(" prio=");
            debug_write_hex(t->priority);
            debug_write(" state=");
            debug_write_hex(static_cast<uint64_t>(t->state));
        }
        debug_write(" tasks=");
        for (uint64_t i = 0; i < kernel::Scheduler::task_count(); ++i) {
            auto* ti = kernel::Scheduler::task_at(i);
            if (ti) {
                debug_write_hex(ti->id);
                debug_write(":");
                debug_write_hex(static_cast<uint64_t>(ti->state));
                debug_write(" ");
            }
        }
        debug_write(" !!!\n");
        panic("CPU EXCEPTION");
        return;
    }

    if (vector == 0x80) {
        regs[0] = syscall_handler(regs[0], regs[1], regs[2], regs[3], regs[4], regs);
        auto* task = kernel::Scheduler::current_task();
        if (task && (task->state == kernel::TaskState::TERMINATED ||
                     task->state == kernel::TaskState::BLOCKED)) {
            kernel::Scheduler::reschedule();
        }
        return;
    }

    arch::IDT::handle_interrupt(vector, error_code, rip);

    if (vector >= 32 && vector < 48) {
        outb(0x20, 0x20);
        if (vector >= 40) outb(0xA0, 0x20);
    }
}

extern "C" uint64_t syscall_handler(uint64_t number, uint64_t arg0, uint64_t arg1,
                                    uint64_t arg2, uint64_t arg3, uint64_t* regs)
{
    if (number == 99) {
        g_user_task_ran = true;
        return 0;
    }
    return kernel::Syscall::handle(number, arg0, arg1, arg2, arg3, regs);
}

uint8_t kernel_stack[16_KiB] __attribute__((section(".bss")));
