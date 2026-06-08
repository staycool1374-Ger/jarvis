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

void register_elf_tests() {
    Logger::info("Registering ELF tests");

    JARVIS_REGISTER_TEST(elf_validate_header_null);
    JARVIS_REGISTER_TEST(elf_validate_header_magic);
    JARVIS_REGISTER_TEST(elf_validate_header_bad_machine);
    JARVIS_REGISTER_TEST(elf_validate_header_excessive_phnum);
    JARVIS_REGISTER_TEST(elf_validate_header_bad_entry);
}
