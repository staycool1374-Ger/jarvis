#include <kernel/elf/elf.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <kernel/arch/gdt.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/task/scheduler.hpp>
#include <assert.hpp>
#include <string.hpp>

namespace kernel {
namespace elf {

static constexpr uint64_t ELF_MAGIC = 0x00000000464C457FULL;
static constexpr uint64_t USER_STACK_SIZE = 64_KiB;
static constexpr uint64_t USER_STACK_VADDR = 0x70000000;
static constexpr uint64_t USER_HEAP_VADDR = 0x60000000;
static constexpr uint64_t USER_HEAP_SIZE = 1_MiB;

bool validate_header(const ELF64Header* hdr) {
    if (!hdr) return false;
    if (reinterpret_cast<const uint32_t*>(hdr->ident)[0] !=
        static_cast<uint32_t>(ELF_MAGIC)) return false;
    if (hdr->ident[4] != 2) return false;
    if (hdr->ident[5] != 1) return false;
    if (hdr->type != ET_EXEC && hdr->type != ET_DYN) return false;
    if (hdr->machine != 0x3E) return false;
    if (hdr->phnum > 64) return false;
    if (hdr->phnum > 0) {
        if (hdr->phentsize < sizeof(ELF64ProgramHeader)) return false;
        uint64_t ph_end = hdr->phoff + static_cast<uint64_t>(hdr->phnum) * hdr->phentsize;
        if (ph_end < hdr->phoff) return false;
    }
    return true;
}

static bool validate_segment(const ELF64ProgramHeader* phdr) {
    if (phdr->type != PT_LOAD) return true;
    if (phdr->filesz > phdr->memsz) return false;
    if (phdr->offset + phdr->filesz < phdr->offset) return false;
    if (phdr->vaddr + phdr->memsz < phdr->vaddr) return false;
    if (phdr->vaddr >= USER_SPACE_LIMIT) return false;
    if (phdr->vaddr + phdr->memsz > USER_SPACE_LIMIT) return false;
    return true;
}

static uint64_t page_align_up(uint64_t addr) {
    return (addr + 0xFFF) & ~0xFFFULL;
}

static uint64_t page_align_down(uint64_t addr) {
    return addr & ~0xFFFULL;
}

static int count_strings(const char* const* arr) {
    if (!arr) return 0;
    int n = 0;
    while (arr[n]) ++n;
    return n;
}

static uint64_t total_string_len(const char* const* arr) {
    if (!arr) return 0;
    uint64_t total = 0;
    for (int i = 0; arr[i]; ++i)
        total += strlen(arr[i]) + 1;
    return total;
}

static void copy_strings(uint8_t* dest, const char* const* arr) {
    if (!arr) return;
    for (int i = 0; arr[i]; ++i) {
        size_t len = strlen(arr[i]) + 1;
        memcpy(dest, arr[i], len);
        dest += len;
    }
}

static bool load_segments_and_stack(const ELF64Header* hdr, const uint8_t* file_data,
                                     uint64_t pml4, uint64_t* out_ustack_phys)
{
    for (uint16_t i = 0; i < hdr->phnum; ++i) {
        auto* phdr = reinterpret_cast<const ELF64ProgramHeader*>(
            file_data + hdr->phoff + i * hdr->phentsize);
        if (!phdr || phdr->type != PT_LOAD) continue;
        if (!validate_segment(phdr)) return false;

        uint64_t vaddr_base = page_align_down(phdr->vaddr);
        uint64_t vaddr_end = page_align_up(phdr->vaddr + phdr->memsz);
        uint64_t region_size = vaddr_end - vaddr_base;
        size_t num_pages = region_size / 4096;

        uint64_t seg_phys = PMM::alloc_contiguous(num_pages);
        if (!seg_phys) return false;

        for (size_t p = 0; p < num_pages; ++p) {
            VMM::map_page_in_pml4(vaddr_base + p * 4096,
                                  seg_phys + p * 4096,
                                  true, pml4);
        }

        uint64_t offset_in_region = phdr->vaddr - vaddr_base;
        memcpy(reinterpret_cast<void*>(seg_phys + offset_in_region),
               file_data + phdr->offset, phdr->filesz);

        if (phdr->memsz > phdr->filesz) {
            memset(reinterpret_cast<void*>(seg_phys + offset_in_region + phdr->filesz),
                   0, phdr->memsz - phdr->filesz);
        }
    }

    size_t ustack_pages = (USER_STACK_SIZE + 4095) / 4096;
    uint64_t ustack_phys = PMM::alloc_contiguous(ustack_pages);
    if (!ustack_phys) return false;

    for (size_t i = 0; i < ustack_pages; ++i) {
        VMM::map_page_in_pml4(USER_STACK_VADDR + i * 4096,
                              ustack_phys + i * 4096,
                              true, pml4);
    }

    memset(reinterpret_cast<void*>(ustack_phys), 0, USER_STACK_SIZE);

    size_t heap_pages = USER_HEAP_SIZE / 4096;
    uint64_t heap_phys = PMM::alloc_contiguous(heap_pages);
    if (!heap_phys) return false;

    for (size_t i = 0; i < heap_pages; ++i) {
        VMM::map_page_in_pml4(USER_HEAP_VADDR + i * 4096,
                              heap_phys + i * 4096,
                              true, pml4);
    }
    memset(reinterpret_cast<void*>(heap_phys), 0, USER_HEAP_SIZE);

    if (out_ustack_phys) *out_ustack_phys = ustack_phys;
    return true;
}

static uint64_t setup_user_stack(uint64_t ustack_phys,
                                  const char* const* argv,
                                  const char* const* envp)
{
    int argc = count_strings(argv);

    uint64_t str_total = total_string_len(argv) + total_string_len(envp);
    uint8_t* stack_top = reinterpret_cast<uint8_t*>(ustack_phys + USER_STACK_SIZE);

    uint8_t* sp = stack_top;
    sp -= str_total;
    if (str_total > 0) {
        uint8_t* str_pos = sp;
        copy_strings(str_pos, envp);
        str_pos += total_string_len(envp);
        copy_strings(str_pos, argv);
    }

    uint64_t align = reinterpret_cast<uint64_t>(sp) & 0xF;
    if (align) sp -= (16 - align);

    int envc = count_strings(envp);
    uint64_t* ptr = reinterpret_cast<uint64_t*>(sp);

    for (int i = envc; i >= 0; --i) {
        *--ptr = 0;
    }
    uint64_t* envp_start = ptr;
    if (envp) {
        uint8_t* str_pos = sp;
        for (int i = 0; i < envc; ++i) {
            envp_start[i] = reinterpret_cast<uint64_t>(str_pos);
            str_pos += strlen(envp[i]) + 1;
        }
    }

    for (int i = argc; i >= 0; --i) {
        *--ptr = 0;
    }
    uint64_t* argv_start = ptr;
    if (argv) {
        uint8_t* str_pos = sp + total_string_len(envp);
        for (int i = 0; i < argc; ++i) {
            argv_start[i] = reinterpret_cast<uint64_t>(str_pos);
            str_pos += strlen(argv[i]) + 1;
        }
    }

    *--ptr = static_cast<uint64_t>(argc);

    uint64_t user_rsp = reinterpret_cast<uint64_t>(ptr);
    user_rsp = user_rsp - reinterpret_cast<uint64_t>(stack_top) + USER_STACK_VADDR + USER_STACK_SIZE;

    return user_rsp;
}

static void open_std_fds(TaskControlBlock* tcb) {
    vfs::Vnode* tty = vfs::resolve("/dev/tty");
    if (!tty) return;
    for (int std_fd = 0; std_fd < 3; ++std_fd) {
        int fd = tcb->fd_table.alloc();
        if (fd == std_fd) {
            tcb->fd_table.fds[fd].vnode = tty;
            tcb->fd_table.fds[fd].offset = 0;
            tcb->fd_table.fds[fd].flags = 0;
            if (tty->ops && tty->ops->open) tty->ops->open(tty, 0);
        }
    }
}

TaskControlBlock* load(const ELF64Header* hdr, const uint8_t* file_data) {
    if (!validate_header(hdr)) return nullptr;

    auto* tcb = new TaskControlBlock{};
    if (!tcb) return nullptr;

    static uint64_t next_task_id = 42;
    tcb->id = next_task_id++;
    tcb->state = TaskState::READY;
    tcb->priority = 5;
    tcb->period_ticks = 20;

    size_t kstack_pages = (TaskControlBlock::STACK_SIZE + 4095) / 4096;
    uint64_t kstack_phys = PMM::alloc_contiguous(kstack_pages);
    if (!kstack_phys) { delete tcb; return nullptr; }
    tcb->stack_phys_ = kstack_phys;
    uint64_t kstack_virt = 0xFFFF800000000000ULL + kstack_phys;
    tcb->kernel_stack = reinterpret_cast<uint8_t*>(kstack_virt);
    tcb->kernel_stack_top = kstack_virt + TaskControlBlock::STACK_SIZE;

    uint64_t pml4 = VMM::clone_kernel_pml4();
    if (!pml4) { delete tcb; return nullptr; }
    tcb->page_table_ = pml4;

    uint64_t ustack_phys = 0;
    if (!load_segments_and_stack(hdr, file_data, pml4, &ustack_phys)) { delete tcb; return nullptr; }

    open_std_fds(tcb);

    tcb->user_stack_ = ustack_phys;
    tcb->user_stack_size_ = USER_STACK_SIZE;

    uint64_t user_rsp = setup_user_stack(ustack_phys, nullptr, nullptr);

    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    *--stack = 0x23;
    *--stack = user_rsp;
    *--stack = 0x202;
    *--stack = 0x1B;
    *--stack = hdr->entry;
    *--stack = 0;
    *--stack = 0;
    for (int j = 0; j < 15; ++j) *--stack = 0;
    tcb->context.rsp = reinterpret_cast<uint64_t>(stack);

    return tcb;
}

bool exec_into_current(const ELF64Header* hdr, const uint8_t* data,
                       const char* const* argv, const char* const* envp,
                       uint64_t* regs)
{
    if (!validate_header(hdr)) return false;

    auto* tcb = Scheduler::current_task();
    if (!tcb) return false;

    uint64_t new_pml4 = VMM::clone_kernel_pml4();
    if (!new_pml4) return false;

    uint64_t ustack_phys = 0;
    if (!load_segments_and_stack(hdr, data, new_pml4, &ustack_phys))
        return false;

    uint64_t user_rsp = setup_user_stack(ustack_phys, argv, envp);

    uint64_t old_pml4 = tcb->page_table_;
    tcb->page_table_ = new_pml4;
    tcb->user_stack_ = ustack_phys;
    tcb->user_stack_size_ = USER_STACK_SIZE;

    {
        vfs::Vnode* tty = vfs::resolve("/dev/tty");
        if (tty && tcb->fd_table.get(0) && tcb->fd_table.get(0)->vnode == tty) {
            // keep std fds
        } else {
            for (int std_fd = 0; std_fd < 3; ++std_fd) {
                if (tcb->fd_table.get(std_fd)) {
                    tcb->fd_table.free(std_fd);
                }
            }
            vfs::Vnode* tty2 = vfs::resolve("/dev/tty");
            if (tty2) {
                for (int std_fd = 0; std_fd < 3; ++std_fd) {
                    int fd = tcb->fd_table.alloc();
                    if (fd == std_fd) {
                        tcb->fd_table.fds[fd].vnode = tty2;
                        tcb->fd_table.fds[fd].offset = 0;
                        tcb->fd_table.fds[fd].flags = 0;
                        if (tty2->ops && tty2->ops->open) tty2->ops->open(tty2, 0);
                    }
                }
            }
        }
    }

    regs[17] = hdr->entry;
    regs[18] = 0x1B;
    regs[19] = 0x202;
    regs[20] = user_rsp;
    regs[21] = 0x23;
    regs[0] = 0;

    {
        uint64_t cr3;
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        if (cr3 != new_pml4) {
            asm volatile("mov %0, %%cr3" : : "r"(new_pml4) : "memory");
        }
    }

    if (old_pml4 && old_pml4 != VMM::get_kernel_pml4()) {
        VMM::free_user_pages(old_pml4);
        PMM::free_page(old_pml4);
    }

    return true;
}

} // namespace elf
} // namespace kernel