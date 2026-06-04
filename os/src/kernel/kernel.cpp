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
#include <kernel/multiboot2.hpp>
#include <services/terminal/framebuffer.hpp>
#include <services/terminal/terminal.hpp>
#include <services/shell.hpp>
#include <services/program.hpp>
#include <programs/demo/demo.hpp>

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

static void debug_write(const char* s) {
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
}

extern "C" {
    uint32_t multiboot_magic = 0;
    uint32_t multiboot_info_ptr = 0;
}
extern char kernel_virt_end[];

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

    debug_write("[BOOT] MemPool init...\n");
    kernel::MemPool::init();
    debug_write("[BOOT] MemPool OK\n");

    debug_write("[BOOT] IPC init...\n");
    kernel::IPC::init();
    debug_write("[BOOT] IPC OK\n");

    debug_write("[BOOT] Syscall init...\n");
    kernel::Syscall::init();
    debug_write("[BOOT] Syscall OK\n");

    debug_write("[BOOT] Scheduler init...\n");
    kernel::Scheduler::init();
    debug_write("[BOOT] Scheduler OK\n");

    debug_write("[BOOT] DriverRegistry init...\n");
    kernel::DriverRegistry::init();
    debug_write("[BOOT] DriverRegistry OK\n");

    debug_write("[BOOT] TestRegistry init...\n");
    kernel::test::TestRegistry::init();
    debug_write("[BOOT] TestRegistry OK\n");

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

    debug_write("[BOOT] Framebuffer init...\n");
    service::Framebuffer::init();
    debug_write("[BOOT] Framebuffer OK\n");

    debug_write("[BOOT] Terminal init...\n");
    service::Terminal::init();
    debug_write("[BOOT] Terminal OK\n");

    debug_write("[BOOT] Timer init...\n");
    arch::Timer::init(DEFAULT_TIMER_HZ);
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

extern "C" void handle_interrupt_c(uint64_t vector, uint64_t error_code, uint64_t rip) {
    if (vector < 32) {
        debug_write("\n!!! EXCEPTION vector=");
        uint64_t v = vector;
        char hb[17] = "0000000000000000";
        for (int i = 15; v && i >= 0; --i) { hb[i] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; }
        hb[16] = 0; debug_write(hb);
        debug_write(" err=");
        v = error_code;
        for (int i = 15; v && i >= 0; --i) { hb[i] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; }
        hb[16] = 0; debug_write(hb);
        debug_write(" rip=");
        v = rip;
        for (int i = 15; v && i >= 0; --i) { hb[i] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; }
        hb[16] = 0; debug_write(hb);
        debug_write(" !!!\n");
        panic("CPU EXCEPTION");
        return;
    }

    arch::IDT::handle_interrupt(vector, error_code, rip);

    if (vector >= 32 && vector < 48) {
        outb(0x20, 0x20);
        if (vector >= 40) outb(0xA0, 0x20);
    }
}

extern "C" void syscall_handler(uint64_t number, uint64_t arg0, uint64_t arg1,
                                uint64_t arg2, uint64_t arg3)
{
    kernel::Syscall::handle(number, arg0, arg1, arg2, arg3);
}

uint8_t kernel_stack[16_KiB] __attribute__((section(".bss")));
