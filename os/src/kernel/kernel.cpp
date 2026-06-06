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
#include <services/program.hpp>
#include <logger.hpp>
#include <test.hpp>
#include <kernel/test/test_selftest.hpp>

static constexpr uint32_t DEFAULT_TIMER_HZ = 1000;

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
            outb(0x20, 0x20);
        }
    );
    debug_write("[BOOT] Keyboard OK\n");

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

    register_selftest_tests();
    kernel::test::run_all();

    debug_write("[BOOT] Starting userspace shell...\n");
    {
        initrd::InitrdFile f = initrd::find("./sh.c.elf");
        if (!f.data) f = initrd::find("sh.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
            if (kernel::elf::validate_header(hdr)) {
                auto* shell_task = kernel::elf::load(hdr, f.data);
                if (shell_task) {
                    shell_task->priority = 1;
                    shell_task->period_ticks = 50;
                    kernel::Scheduler::add_task(shell_task);
                    debug_write("[BOOT] Userspace shell started (PID=");
                    debug_write_hex(shell_task->id);
                    debug_write(")\n");
                }
            }
        }
    }

    debug_write("[BOOT] Boot complete!\n");
    arch::qemu_debug_exit(0x01);
    debug_write("[BOOT] Enabling interrupts...\n");
    sti();

    debug_write("[BOOT] Entering idle loop.\n");
    while (true) {
        asm volatile("hlt");
    }
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
    while (true) {
        asm volatile("hlt");
    }
}

static void dump_regs(uint64_t* regs) {
    if (!regs) return;

    using L = kernel::Logger;

    L::raw_write("  RAX: "); L::print_hex(regs[0]);  L::raw_write("  RBX: "); L::print_hex(regs[1]);  L::raw_write("\n");
    L::raw_write("  RCX: "); L::print_hex(regs[2]);  L::raw_write("  RDI: "); L::print_hex(regs[5]);  L::raw_write("\n");
    L::raw_write("  RDX: "); L::print_hex(regs[3]);  L::raw_write("  RSI: "); L::print_hex(regs[4]);  L::raw_write("\n");
    L::raw_write("  RBP: "); L::print_hex(regs[6]);  L::raw_write("  RSP: "); L::print_hex(reinterpret_cast<uint64_t>(&regs)); L::raw_write("\n");
    L::raw_write("  R8:  "); L::print_hex(regs[7]);  L::raw_write("  R9:  "); L::print_hex(regs[8]);  L::raw_write("\n");
    L::raw_write("  R10: "); L::print_hex(regs[9]);  L::raw_write("  R11: "); L::print_hex(regs[10]); L::raw_write("\n");
    L::raw_write("  R12: "); L::print_hex(regs[11]); L::raw_write("  R13: "); L::print_hex(regs[12]); L::raw_write("\n");
    L::raw_write("  R14: "); L::print_hex(regs[13]); L::raw_write("  R15: "); L::print_hex(regs[14]); L::raw_write("\n");
    L::raw_write("  RIP: "); L::print_hex(regs[17]); L::raw_write("  CS:  "); L::print_hex(regs[18]); L::raw_write("\n");
    L::raw_write("  RFL: "); L::print_hex(regs[19]); L::raw_write("\n");

    uint64_t cr0, cr2, cr3, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));

    L::raw_write("  CR0: "); L::print_hex(cr0); L::raw_write("\n");
    L::raw_write("  CR2: "); L::print_hex(cr2); L::raw_write("  CR3: "); L::print_hex(cr3); L::raw_write("\n");
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

            kernel::Logger::error("Task %x received %s vector=%x rip=%x err=%x",
                t->id, sig_name[sig_idx], vector, rip, error_code);
            if (vector == 14) {
                uint64_t cr2_val;
                asm volatile("mov %%cr2, %0" : "=r"(cr2_val));
                kernel::Logger::error("  CR2=%x", cr2_val);
            }
            dump_regs(regs);
            t->state = kernel::TaskState::TERMINATED;
            t->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig_num[sig_idx]));
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
    return kernel::Syscall::handle(number, arg0, arg1, arg2, arg3, regs);
}

uint8_t kernel_stack[16_KiB] __attribute__((section(".bss")));
