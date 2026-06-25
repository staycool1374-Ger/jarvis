ENTRY(_start)

PHDRS
{
    boot_text PT_LOAD FLAGS(5);
    boot_data PT_LOAD FLAGS(6);
    text      PT_LOAD FLAGS(5);
    rodata    PT_LOAD FLAGS(4);
    data      PT_LOAD FLAGS(6);
}

SECTIONS
{
    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.init_array)
        *(.init_array.*)
    }

    . = 0x40080000;

    .boot_text : ALIGN(4K)
    {
        *(.boot.text)
        *(.boot.rodata)
    } :boot_text

    .boot_data : ALIGN(4K)
    {
        *(.boot.data)
        *(.boot.bss)
    } :boot_data

    .boot_stack : ALIGN(4K)
    {
        *(.boot_stack)
    } :boot_data

    . = ALIGN(., 4K);
    boot_vma_end = .;
    phys_lma = boot_vma_end;

    . = 0xFFFF800000000000 + phys_lma;

    .text : AT(phys_lma) ALIGN(4K)
    {
        _text_start = .;
        *(.vectors)
        *(.text*)
        *(.text.*)
        _text_end = .;
    } :text
    phys_lma = ALIGN(phys_lma + SIZEOF(.text), 4K);

    .rodata : AT(phys_lma) ALIGN(4K)
    {
        _rodata_start = .;
        *(.rodata*)
        *(.rodata.*)
        . = ALIGN(8);
        _expected_code_crc = .;
        QUAD(0x0000000000000000)
        _rodata_end = .;
    } :rodata
    phys_lma = ALIGN(phys_lma + SIZEOF(.rodata), 4K);

    .data : AT(phys_lma) ALIGN(4K)
    {
        _data_start = .;
        *(.data*)
        *(.data.*)
        *(.got)
        *(.got.plt)
        _data_end = .;
    } :data
    phys_lma = ALIGN(phys_lma + SIZEOF(.data), 4K);

    .bss : AT(phys_lma) ALIGN(4K)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss*)
        *(.bss.*)
        _bss_end = .;
    } :data

    kernel_phys_end = ALIGN(phys_lma + SIZEOF(.bss), 4K);
    kernel_virt_end = ALIGN(., 4K);
}
