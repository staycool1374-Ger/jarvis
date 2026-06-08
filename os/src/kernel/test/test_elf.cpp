#include <test.hpp>
#include <logger.hpp>
#include <kernel/elf/elf.hpp>

using namespace kernel;

JARVIS_TEST(elf_validate_header_null) {
    JARVIS_ASSERT(!elf::validate_header(nullptr));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(elf_validate_header_magic) {
    elf::ELF64Header hdr{};
    hdr.ident[0] = 0x7F;
    hdr.ident[1] = 'E';
    hdr.ident[2] = 'L';
    hdr.ident[3] = 'F';
    hdr.ident[4] = 2;
    hdr.ident[5] = 1;
    hdr.ident[6] = 1;
    hdr.type = 2;
    hdr.machine = 0x3E;
    hdr.version = 1;
    hdr.ehsize = sizeof(elf::ELF64Header);
    hdr.phentsize = sizeof(elf::ELF64ProgramHeader);
    hdr.phnum = 0;
    hdr.entry = 0x400000;

    JARVIS_ASSERT(elf::validate_header(&hdr));

    hdr.ident[0] = 0;
    JARVIS_ASSERT(!elf::validate_header(&hdr));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(elf_validate_header_bad_machine) {
    elf::ELF64Header hdr{};
    hdr.ident[0] = 0x7F;
    hdr.ident[1] = 'E';
    hdr.ident[2] = 'L';
    hdr.ident[3] = 'F';
    hdr.ident[4] = 2;
    hdr.ident[5] = 1;
    hdr.ident[6] = 1;
    hdr.type = 2;
    hdr.machine = 0x3E;
    hdr.version = 1;
    hdr.ehsize = sizeof(elf::ELF64Header);
    hdr.phentsize = sizeof(elf::ELF64ProgramHeader);
    hdr.phnum = 0;
    hdr.entry = 0x400000;

    hdr.machine = 0x28;
    JARVIS_ASSERT(!elf::validate_header(&hdr));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(elf_validate_header_excessive_phnum) {
    elf::ELF64Header hdr{};
    hdr.ident[0] = 0x7F;
    hdr.ident[1] = 'E';
    hdr.ident[2] = 'L';
    hdr.ident[3] = 'F';
    for (int i = 7; i < 16; ++i) hdr.ident[i] = 0;
    hdr.ident[4] = 2;
    hdr.ident[5] = 1;
    hdr.ident[6] = 1;
    hdr.type = 2;
    hdr.machine = 0x3E;
    hdr.version = 1;
    hdr.ehsize = sizeof(elf::ELF64Header);
    hdr.phentsize = sizeof(elf::ELF64ProgramHeader);
    hdr.phnum = 100;
    hdr.entry = 0x400000;

    JARVIS_ASSERT(!elf::validate_header(&hdr));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(elf_validate_header_bad_entry) {
    elf::ELF64Header hdr{};
    hdr.ident[0] = 0x7F;
    hdr.ident[1] = 'E';
    hdr.ident[2] = 'L';
    hdr.ident[3] = 'F';
    for (int i = 7; i < 16; ++i) hdr.ident[i] = 0;
    hdr.ident[4] = 2;
    hdr.ident[5] = 1;
    hdr.ident[6] = 1;
    hdr.type = 2;
    hdr.machine = 0x3E;
    hdr.version = 1;
    hdr.ehsize = sizeof(elf::ELF64Header);
    hdr.phentsize = sizeof(elf::ELF64ProgramHeader);
    hdr.phnum = 0;
    hdr.entry = 0xFFFF800000000000ULL;

    JARVIS_ASSERT(!elf::validate_header(&hdr));
    JARVIS_TEST_PASS();
}

static void build_minimal_elf(elf::ELF64Header* hdr, elf::ELF64ProgramHeader* phdr, uint8_t* data) {
    hdr->ident[0] = 0x7F;
    hdr->ident[1] = 'E';
    hdr->ident[2] = 'L';
    hdr->ident[3] = 'F';
    hdr->ident[4] = 2;
    hdr->ident[5] = 1;
    hdr->ident[6] = 1;
    for (int i = 7; i < 16; ++i) hdr->ident[i] = 0;
    hdr->type = elf::ET_EXEC;
    hdr->machine = 0x3E;
    hdr->version = 1;
    hdr->entry = 0x400000;
    hdr->phoff = sizeof(elf::ELF64Header);
    hdr->shoff = 0;
    hdr->flags = 0;
    hdr->ehsize = sizeof(elf::ELF64Header);
    hdr->phentsize = sizeof(elf::ELF64ProgramHeader);
    hdr->phnum = 1;
    hdr->shentsize = 0;
    hdr->shnum = 0;
    hdr->shstrndx = 0;

    phdr->type = elf::PT_LOAD;
    phdr->flags = 5;
    phdr->offset = sizeof(elf::ELF64Header) + sizeof(elf::ELF64ProgramHeader);
    phdr->vaddr = 0x400000;
    phdr->paddr = 0x400000;
    phdr->filesz = 0x1000;
    phdr->memsz = 0x1000;
    phdr->align = 0x1000;

    for (size_t i = 0; i < 0x1000; ++i) {
        data[i] = 0x90;
    }
}

JARVIS_TEST(elf_load_invalid_segment) {
    elf::ELF64Header hdr{};
    elf::ELF64ProgramHeader phdr{};
    uint8_t data[4096];

    build_minimal_elf(&hdr, &phdr, data);

    phdr.vaddr = 0xFFFF800000000000ULL;
    auto* tcb = elf::load(&hdr, data);
    JARVIS_ASSERT(tcb == nullptr);

    build_minimal_elf(&hdr, &phdr, data);
    phdr.memsz = 0;
    tcb = elf::load(&hdr, data);
    JARVIS_ASSERT(tcb == nullptr);

    build_minimal_elf(&hdr, &phdr, data);
    phdr.align = 2_MiB;
    tcb = elf::load(&hdr, data);
    JARVIS_ASSERT(tcb == nullptr);

    build_minimal_elf(&hdr, &phdr, data);
    phdr.filesz = 0x2000;
    phdr.memsz = 0x1000;
    tcb = elf::load(&hdr, data);
    JARVIS_ASSERT(tcb == nullptr);

    JARVIS_TEST_PASS();
}

JARVIS_TEST(elf_load_sets_std_fds) {
    elf::ELF64Header hdr{};
    elf::ELF64ProgramHeader phdr{};
    uint8_t data[4096];
    build_minimal_elf(&hdr, &phdr, data);

    auto* tcb = elf::load(&hdr, data);
    JARVIS_ASSERT(tcb != nullptr);

    auto* tty = vfs::resolve("/dev/tty");
    JARVIS_ASSERT(tty != nullptr);

    for (int fd = 0; fd < 3; ++fd) {
        auto* f = tcb->fd_table.get(fd);
        JARVIS_ASSERT(f != nullptr);
        JARVIS_ASSERT(f->used);
        JARVIS_ASSERT(f->vnode == tty);
    }

    tcb->cleanup();
    delete tcb;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(elf_load_creates_user_stack) {
    elf::ELF64Header hdr{};
    elf::ELF64ProgramHeader phdr{};
    uint8_t data[4096];
    build_minimal_elf(&hdr, &phdr, data);

    auto* tcb = elf::load(&hdr, data);
    JARVIS_ASSERT(tcb != nullptr);
    JARVIS_ASSERT(tcb->user_stack_ != 0);
    JARVIS_ASSERT_EQ(mem::STACK_SIZE, tcb->user_stack_size_);

    tcb->cleanup();
    delete tcb;
    JARVIS_TEST_PASS();
}

void register_elf_tests() {
    Logger::info("Registering ELF tests");

    JARVIS_REGISTER_TEST(elf_validate_header_null);
    JARVIS_REGISTER_TEST(elf_validate_header_magic);
    JARVIS_REGISTER_TEST(elf_validate_header_bad_machine);
    JARVIS_REGISTER_TEST(elf_validate_header_excessive_phnum);
    JARVIS_REGISTER_TEST(elf_validate_header_bad_entry);
}

JARVIS_REGISTER_TEST(elf_load_invalid_segment);
JARVIS_REGISTER_TEST(elf_load_sets_std_fds);
JARVIS_REGISTER_TEST(elf_load_creates_user_stack);
}
